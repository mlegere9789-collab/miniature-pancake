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
        var currentVersion = typeof(GitHubReleaseUpdateChecker).Assembly.GetName().Version ?? new Version(0, 0, 0);
        var currentVersionText = ReleaseVersionParser.Format(currentVersion);

        try
        {
            using var response = await _http.GetAsync(ReleasesApiUrl, cancellationToken).ConfigureAwait(false);
            response.EnsureSuccessStatusCode();

            await using var stream = await response.Content.ReadAsStreamAsync(cancellationToken).ConfigureAwait(false);
            using var document = await JsonDocument.ParseAsync(stream, cancellationToken: cancellationToken).ConfigureAwait(false);

            var tagName = document.RootElement.TryGetProperty("tag_name", out var tagProperty)
                ? tagProperty.GetString()
                : null;

            var downloadUrl = document.RootElement.TryGetProperty("html_url", out var urlProperty)
                ? urlProperty.GetString()
                : null;

            var latestVersion = ReleaseVersionParser.Parse(tagName);

            return new UpdateCheckResult
            {
                HasUpdate = ReleaseVersionParser.IsNewerThan(latestVersion, currentVersion),
                CurrentVersion = currentVersionText,
                LatestVersion = tagName,
                DownloadUrl = downloadUrl,
            };
        }
        catch (Exception ex) when (ex is HttpRequestException or TaskCanceledException or JsonException)
        {
            // Offline, GitHub unreachable, rate-limited, an unexpected response shape —
            // none of these are the user's problem. The banner just stays hidden.
            return new UpdateCheckResult
            {
                HasUpdate = false,
                CurrentVersion = currentVersionText,
                ErrorMessage = ex.Message,
            };
        }
    }

    public void Dispose()
    {
        if (_ownsClient)
        {
            _http.Dispose();
        }
    }
}
