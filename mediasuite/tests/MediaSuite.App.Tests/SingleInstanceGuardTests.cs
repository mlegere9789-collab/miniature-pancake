using MediaSuite.App.Services;
using Xunit;

namespace MediaSuite.App.Tests;

/// <summary>
/// Exercises the real named Mutex and named pipe mechanics <see cref="SingleInstanceGuard"/>
/// relies on, not just that the class compiles — both halves of a real launch (the
/// already-running instance, and a second launch forwarding to it) run within this one
/// test process, which is a faithful stand-in since Windows mutexes and pipes are named OS
/// objects a second real process would see identically. Every test here shares the guard's
/// hardcoded mutex/pipe names, so they rely on xunit's default same-class sequential
/// execution — do not mark this class or its methods to run in parallel.
/// </summary>
public class SingleInstanceGuardTests
{
    [Fact]
    public void The_first_Acquire_owns_the_instance()
    {
        using var guard = SingleInstanceGuard.Acquire();

        Assert.True(guard.IsFirstInstance);
    }

    [Fact]
    public void A_second_Acquire_while_the_first_is_still_held_is_not_first()
    {
        using var first = SingleInstanceGuard.Acquire();
        using var second = SingleInstanceGuard.Acquire();

        Assert.True(first.IsFirstInstance);
        Assert.False(second.IsFirstInstance);
    }

    [Fact]
    public void Disposing_the_first_instance_lets_a_later_Acquire_become_first_again()
    {
        var first = SingleInstanceGuard.Acquire();
        Assert.True(first.IsFirstInstance);
        first.Dispose();

        using var next = SingleInstanceGuard.Acquire();

        Assert.True(next.IsFirstInstance);
    }

    [Fact]
    public void Forwarding_files_to_a_listening_instance_delivers_them()
    {
        using var owner = SingleInstanceGuard.Acquire();
        Assert.True(owner.IsFirstInstance);

        var receivedFiles = new TaskCompletionSource<IReadOnlyList<string>>();
        owner.StartListening(files => receivedFiles.TrySetResult(files));

        var sent = new[] { @"C:\Users\someone\Videos\clip.mp4", @"C:\Users\someone\Pictures\photo.jpg" };

        // No artificial delay before forwarding: this relies on the same connect-retry
        // behaviour App.OnStartup's production code depends on to close the window between
        // the mutex being claimed and the listener actually being up.
        SingleInstanceGuard.ForwardToRunningInstance(sent);

        // A blocking Wait, not await: Mutex.ReleaseMutex (inside owner's Dispose below)
        // demands the exact OS thread that acquired it, and an awaited continuation is
        // not guaranteed to resume on that same thread. Keeping this whole test method
        // synchronous keeps Acquire and Dispose on the one thread throughout -- xUnit1031
        // exists for a real reason in general, but following it here would reintroduce
        // the exact ApplicationException this pattern was chosen to avoid.
#pragma warning disable xUnit1031
        Assert.True(receivedFiles.Task.Wait(TimeSpan.FromSeconds(5)), "Timed out waiting for the forwarded files.");

        Assert.Equal(sent, receivedFiles.Task.Result);
#pragma warning restore xUnit1031
    }

    [Fact]
    public void Forwarding_with_no_files_still_invokes_the_callback()
    {
        using var owner = SingleInstanceGuard.Acquire();

        var invoked = new TaskCompletionSource<IReadOnlyList<string>>();
        owner.StartListening(files => invoked.TrySetResult(files));

        SingleInstanceGuard.ForwardToRunningInstance(Array.Empty<string>());

        // See the comment in the test above: blocking Wait, not await, to keep Acquire
        // and Dispose (via the using above) on the same OS thread.
#pragma warning disable xUnit1031
        Assert.True(invoked.Task.Wait(TimeSpan.FromSeconds(5)), "Timed out waiting for the callback.");

        Assert.Empty(invoked.Task.Result);
#pragma warning restore xUnit1031
    }
}
