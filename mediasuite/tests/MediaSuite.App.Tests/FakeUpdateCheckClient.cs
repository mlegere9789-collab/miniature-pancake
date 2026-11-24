using System.Threading;
using System.Threading.Tasks;
using MediaSuite.Core.Updates;

namespace MediaSuite.App.Tests;

/// <summary>
/// Stand-in update checker for MainViewModelTests. Always returns an already-completed
/// task, same reasoning as the other fakes in this project: MainViewModel fires its check
/// with a bare <c>_ = CheckForUpdateAsync()</c>, and awaiting an already-complete task
/// continues synchronously on the calling thread, so the whole check runs to completion
/// inside the constructor call that triggered it.
/// </summary>
public sealed class FakeUpdateCheckClient : IUpdateCheckClient
{
    public UpdateCheckResult Result { get; set; } = new() { HasUpdate = false, CurrentVersion = "1.0.0" };

    public Task<UpdateCheckResult> CheckAsync(CancellationToken cancellationToken) => Task.FromResult(Result);
}
