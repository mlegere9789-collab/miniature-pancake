using System.Net;
using MediaSuite.Core.Updates;
using Xunit;

namespace MediaSuite.Core.Tests;

/// <summary>Stands in for GitHub's API so the checker's response handling is tested without a real network call.</summary>
public sealed class FakeHttpMessageHandler : HttpMessageHandler
{
    private readonly Func<HttpResponseMessage> _respond;

    public FakeHttpMessageHandler(Func<HttpResponseMessage> respond) => _respond = respond;

    public static FakeHttpMessageHandler Json(HttpStatusCode statusCode, string json) =>
        new(() => new HttpResponseMessage(statusCode) { Content = new StringContent(json) });

    protected override Task<HttpResponseMessage> SendAsync(HttpRequestMessage request, CancellationToken cancellationToken) =>
        Task.FromResult(_respond());
}

/// <summary>
/// An <see cref="HttpContent"/> whose body throws <see cref="IOException"/> as soon as
/// something tries to read it — standing in for a connection dropping mid-download, which
/// surfaces as a plain IOException rather than an HttpRequestException.
/// </summary>
public sealed class ThrowingContent : HttpContent
{
    protected override Task SerializeToStreamAsync(Stream stream, TransportContext? context) =>
        throw new IOException("simulated connection reset mid-download");

    protected override bool TryComputeLength(out long length)
    {
        length = 0;
        return false;
    }

    protected override Task<Stream> CreateContentReadStreamAsync() =>
        throw new IOException("simulated connection reset mid-download");
}

/// <summary>
/// Exercises the real response handling in <see cref="GitHubReleaseUpdateChecker"/> — the
/// JSON parsing, the fail-soft catch around a broad set of exception types, and how a
/// missing/malformed field turns into a result rather than an unhandled throw — through a
/// fake <see cref="HttpMessageHandler"/> rather than a live call to GitHub's API. Plain
/// net8.0 and HttpClient-based, so no Windows dependency, unlike most of what MediaSuite.App
/// shells out to.
/// </summary>
public class GitHubReleaseUpdateCheckerTests
{
    private static GitHubReleaseUpdateChecker MakeChecker(HttpMessageHandler handler) =>
        new(new HttpClient(handler));

    [Fact]
    public async Task A_newer_release_tag_reports_an_update_with_its_download_url()
    {
        using var checker = MakeChecker(FakeHttpMessageHandler.Json(
            HttpStatusCode.OK,
            """{"tag_name": "v999.0.0", "html_url": "https://github.com/mlegere9789-collab/miniature-pancake/releases/tag/v999.0.0"}"""));

        var result = await checker.CheckAsync(CancellationToken.None);

        Assert.True(result.HasUpdate);
        Assert.Equal("v999.0.0", result.LatestVersion);
        Assert.Equal("https://github.com/mlegere9789-collab/miniature-pancake/releases/tag/v999.0.0", result.DownloadUrl);
        Assert.Null(result.ErrorMessage);
    }

    [Fact]
    public async Task An_older_or_equal_release_tag_reports_no_update()
    {
        using var checker = MakeChecker(FakeHttpMessageHandler.Json(
            HttpStatusCode.OK,
            """{"tag_name": "v0.0.1", "html_url": "https://example.com"}"""));

        var result = await checker.CheckAsync(CancellationToken.None);

        Assert.False(result.HasUpdate);
        Assert.Null(result.ErrorMessage);
    }

    [Fact]
    public async Task A_missing_tag_name_reports_no_update_without_throwing()
    {
        using var checker = MakeChecker(FakeHttpMessageHandler.Json(HttpStatusCode.OK, """{"html_url": "https://example.com"}"""));

        var result = await checker.CheckAsync(CancellationToken.None);

        Assert.False(result.HasUpdate);
        Assert.Null(result.LatestVersion);
        Assert.Null(result.ErrorMessage);
    }

    [Fact]
    public async Task A_non_string_tag_name_is_treated_as_absent_rather_than_trusted()
    {
        // GitHub's API always types tag_name as a string; this is a response this app does
        // not control, so a shape it doesn't expect should degrade rather than throw.
        using var checker = MakeChecker(FakeHttpMessageHandler.Json(HttpStatusCode.OK, """{"tag_name": 123}"""));

        var result = await checker.CheckAsync(CancellationToken.None);

        Assert.False(result.HasUpdate);
        Assert.Null(result.LatestVersion);
        Assert.Null(result.ErrorMessage);
    }

    [Fact]
    public async Task Malformed_JSON_is_caught_and_reported_as_a_failed_check_not_an_exception()
    {
        using var checker = MakeChecker(FakeHttpMessageHandler.Json(HttpStatusCode.OK, "{ this is not valid json"));

        var result = await checker.CheckAsync(CancellationToken.None);

        Assert.False(result.HasUpdate);
        Assert.NotNull(result.ErrorMessage);
    }

    [Fact]
    public async Task A_non_success_status_code_is_caught_and_reported_as_a_failed_check()
    {
        using var checker = MakeChecker(FakeHttpMessageHandler.Json(HttpStatusCode.InternalServerError, "{}"));

        var result = await checker.CheckAsync(CancellationToken.None);

        Assert.False(result.HasUpdate);
        Assert.NotNull(result.ErrorMessage);
    }

    [Fact]
    public async Task A_rate_limit_response_is_caught_and_reported_as_a_failed_check()
    {
        using var checker = MakeChecker(FakeHttpMessageHandler.Json(HttpStatusCode.Forbidden, """{"message": "API rate limit exceeded"}"""));

        var result = await checker.CheckAsync(CancellationToken.None);

        Assert.False(result.HasUpdate);
        Assert.NotNull(result.ErrorMessage);
    }

    [Fact]
    public async Task Every_failed_check_still_reports_the_running_app_s_own_current_version()
    {
        using var checker = MakeChecker(FakeHttpMessageHandler.Json(HttpStatusCode.ServiceUnavailable, "{}"));

        var result = await checker.CheckAsync(CancellationToken.None);

        Assert.False(string.IsNullOrWhiteSpace(result.CurrentVersion));
    }

    [Fact]
    public async Task A_cancelled_request_is_caught_and_reported_as_a_failed_check()
    {
        var handler = new FakeHttpMessageHandler(() => throw new TaskCanceledException("simulated timeout"));
        using var checker = MakeChecker(handler);

        var result = await checker.CheckAsync(CancellationToken.None);

        Assert.False(result.HasUpdate);
        Assert.NotNull(result.ErrorMessage);
    }

    [Fact]
    public async Task A_connection_dropped_mid_download_is_caught_and_reported_as_a_failed_check()
    {
        // Simulates a real network hiccup partway through the response body: GetAsync itself
        // succeeds, but reading the content stream (ReadAsStreamAsync/JsonDocument.ParseAsync)
        // throws IOException rather than HttpRequestException. Before this was added to the
        // catch filter, that IOException escaped CheckAsync uncaught — and since CheckAsync is
        // invoked fire-and-forget from MainViewModel's constructor with no try/catch of its
        // own, it would have become a silent unobserved task exception instead of just hiding
        // the update banner like every other failure mode here does.
        var handler = new FakeHttpMessageHandler(() => new HttpResponseMessage(HttpStatusCode.OK)
        {
            Content = new ThrowingContent(),
        });
        using var checker = MakeChecker(handler);

        var result = await checker.CheckAsync(CancellationToken.None);

        Assert.False(result.HasUpdate);
        Assert.NotNull(result.ErrorMessage);
    }

    [Fact]
    public async Task An_externally_supplied_HttpClient_is_never_disposed_by_the_checker()
    {
        using var handler = FakeHttpMessageHandler.Json(HttpStatusCode.OK, "{}");
        using var externalClient = new HttpClient(handler);
        var checker = new GitHubReleaseUpdateChecker(externalClient);

        checker.Dispose();

        // A disposed HttpClient throws ObjectDisposedException on its next send — actually
        // sending through it again (not just touching an unrelated property) is what proves
        // Dispose() on the checker left a client it doesn't own alone.
        using var response = await externalClient.GetAsync("https://example.invalid/", CancellationToken.None);
        Assert.Equal(HttpStatusCode.OK, response.StatusCode);
    }

    [Fact]
    public void The_GitHub_User_Agent_header_is_set_even_on_an_externally_supplied_client()
    {
        using var handler = FakeHttpMessageHandler.Json(HttpStatusCode.OK, "{}");
        using var externalClient = new HttpClient(handler);
        using var checker = new GitHubReleaseUpdateChecker(externalClient);

        Assert.NotEmpty(externalClient.DefaultRequestHeaders.UserAgent);
    }
}
