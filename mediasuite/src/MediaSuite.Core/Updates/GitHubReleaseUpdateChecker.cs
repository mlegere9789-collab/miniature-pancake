using System.Text.Json;

namespace MediaSuite.Core.Updates;

/// <summary>
/// The real update checker: GitHub's own public releases API for this project's repo.
/// There is no separate update server for a personal build — the repo's own Releases
/// page already is one, and GitHub's API needs no authentication to read it.
/// </summary>
public sealed class GitHubReleaseUpdateChecker : IUpdateCheckClient, IDisposable
{
    private const string ReleasesApiUrl =
        "https://api.github.com/repos/mlegere9789-collab/miniature-pancake/releases/latest";

    private readonly HttpClient _http;
    private readonly bool _ownsClient;

    public GitHubReleaseUpdateChecker(HttpClient? httpClient = null)
    {
        _http = httpClient ?? new HttpClient();
        _ownsClient = httpClient is null;

        // The GitHub API rejects requests with no User-Agent at all.
        if (_http.DefaultRequestHeaders.UserAgent.Count == 0)
        {
            _http.DefaultRequestHeaders.UserAgent.ParseAdd("MediaSuite-UpdateCheck");
        }
    }

    public async Task<UpdateCheckResult> CheckAsync(CancellationToken cancellationToken)
    {
        // MediaSuite.Core's own assembly version, not MediaSuite.App's — correct today
        // only because Directory.Build.props gives every project in the solution the
        // same <Version>. If Core and App ever version independently, this needs to
        // read the App assembly's version instead.
        var currentVersion = typeof(GitHubReleaseUpdateChecker).Assembly.GetName().Version ?? new Version(0, 0, 0);
        var currentVersionText = ReleaseVersionParser.Format(currentVersion);

        try
        {
            using var response = await _http.GetAsync(ReleasesApiUrl, cancellationToken).ConfigureAwait(false);
            response.EnsureSuccessStatusCode();

            await using var stream = await response.Content.ReadAsStreamAsync(cancellationToken).ConfigureAwait(false);
            using var document = await JsonDocument.ParseAsync(stream, cancellationToken: cancellationToken).ConfigureAwait(false);

            var tagName = GetStringOrNull(document.RootElement, "tag_name");
            var downloadUrl = GetStringOrNull(document.RootElement, "html_url");

            var latestVersion = ReleaseVersionParser.Parse(tagName);

            return new UpdateCheckResult
            {
                HasUpdate = ReleaseVersionParser.IsNewerThan(latestVersion, currentVersion),
                CurrentVersion = currentVersionText,
                LatestVersion = tagName,
                DownloadUrl = downloadUrl,
            };
        }
        catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException or JsonException
            or InvalidOperationException or IOException)
        {
            // Offline, GitHub unreachable, rate-limited, an unexpected response shape, or the
            // connection dropping mid-download (ReadAsStreamAsync/JsonDocument.ParseAsync throw
            // a plain IOException for that, not HttpRequestException) — none of these are the
            // user's problem. The banner just stays hidden. This is also this app's only real
            // defense against that IOException: CheckForUpdateAsync calls CheckAsync fire-and-
            // forget from MainViewModel's constructor with no try/catch of its own, since an
            // update check is never supposed to be able to fail loudly — an exception escaping
            // here would become a silent unobserved task exception instead.
            return new UpdateCheckResult
            {
                HasUpdate = false,
                CurrentVersion = currentVersionText,
                ErrorMessage = ex.Message,
            };
        }
    }

    /// <summary>
    /// A string property, or null when it's absent or not actually a string — GitHub's
    /// API always types these two fields as strings, but this reads a response this app
    /// does not control, so it is not trusted to stay that way without a check.
    /// </summary>
    private static string? GetStringOrNull(JsonElement element, string propertyName) =>
        element.TryGetProperty(propertyName, out var property) && property.ValueKind == JsonValueKind.String
            ? property.GetString()
            : null;

    public void Dispose()
    {
        if (_ownsClient)
        {
            _http.Dispose();
        }
    }
}
