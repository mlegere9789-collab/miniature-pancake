using System.IO.Pipes;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;

namespace MediaSuite.App.Services;

/// <summary>
/// Keeps MediaSuite to one running window. Without this, opening a second file — via
/// "Open with", dragging one onto the exe, or just double-clicking a shortcut again —
/// launches a whole redundant second instance with its own job queue, settings writer
/// and tool discovery, instead of the file simply landing in the window already open.
/// A named <see cref="Mutex"/> decides who is first; every later launch forwards its
/// file arguments to that first instance over a named pipe and exits immediately rather
/// than doing any of its own startup work.
/// </summary>
public sealed class SingleInstanceGuard : IDisposable
{
    // "Local\" scopes these to the current login session, matching a per-user install;
    // that also means signed-in-twice (e.g. Remote Desktop plus a local session) each
    // still gets its own instance rather than fighting over one.
    private const string MutexName = "Local\\MediaSuite.SingleInstance";
    private const string PipeName = "MediaSuite.SingleInstance.Pipe";

    // Windows only lets a process steal the foreground if it was the one that most
    // recently received real user input — which this second launch was (a double-click,
    // an "Open with" pick), but the running first instance calling Activate() a moment
    // later was not, so without this its window would just flash in the taskbar instead
    // of actually coming to the front. -1 (ASFW_ANY) grants the *next* SetForegroundWindow
    // call, from any process, that one-time allowance.
    [DllImport("user32.dll")]
    private static extern bool AllowSetForegroundWindow(int dwProcessId);

    private const int AsfwAny = -1;

    private readonly Mutex _mutex;
    private CancellationTokenSource? _listenerCts;

    private SingleInstanceGuard(Mutex mutex, bool isFirstInstance)
    {
        _mutex = mutex;
        IsFirstInstance = isFirstInstance;
    }

    /// <summary>False means another instance already holds the mutex and owns the window.</summary>
    public bool IsFirstInstance { get; }

    /// <summary>Claims the mutex, or discovers it is already held.</summary>
    public static SingleInstanceGuard Acquire()
    {
        var mutex = new Mutex(initiallyOwned: true, MutexName, out var createdNew);
        return new SingleInstanceGuard(mutex, createdNew);
    }

    /// <summary>
    /// Sends this launch's already-resolved file paths to the running instance. Safe to
    /// call with an empty list — a plain second launch with nothing to open still gets
    /// the running window brought to the front. Only meaningful when
    /// <see cref="IsFirstInstance"/> is false; swallows every failure, since if the
    /// running instance is gone or unresponsive this launch has nothing further to do
    /// either way — it was only ever going to hand the file off, never open its own copy.
    /// </summary>
    public static void ForwardToRunningInstance(IReadOnlyList<string> files)
    {
        // Grant it before attempting the handoff, not after: it only needs to happen
        // once, and it costs nothing if the pipe connect below then fails.
        AllowSetForegroundWindow(AsfwAny);

        try
        {
            using var client = new NamedPipeClientStream(".", PipeName, PipeDirection.Out);
            client.Connect(2000);

            using var writer = new StreamWriter(client) { AutoFlush = true };
            foreach (var file in files)
            {
                writer.WriteLine(file);
            }
        }
        catch (Exception ex) when (ex is IOException or TimeoutException or UnauthorizedAccessException)
        {
        }
    }

    /// <summary>
    /// Starts listening for file paths forwarded by future second launches, invoking
    /// <paramref name="onFilesReceived"/> (possibly with an empty list, for "just show
    /// yourself") on a background thread — the caller marshals back to the UI thread.
    /// Only meaningful when <see cref="IsFirstInstance"/> is true.
    /// </summary>
    public void StartListening(Action<IReadOnlyList<string>> onFilesReceived)
    {
        _listenerCts = new CancellationTokenSource();
        _ = ListenLoopAsync(onFilesReceived, _listenerCts.Token);
    }

    private static async Task ListenLoopAsync(Action<IReadOnlyList<string>> onFilesReceived, CancellationToken cancellationToken)
    {
        while (!cancellationToken.IsCancellationRequested)
        {
            try
            {
                await using var server = new NamedPipeServerStream(
                    PipeName, PipeDirection.In, 1, PipeTransmissionMode.Byte, PipeOptions.Asynchronous);
                await server.WaitForConnectionAsync(cancellationToken).ConfigureAwait(false);

                using var reader = new StreamReader(server);
                var files = new List<string>();
                string? line;
                while ((line = await reader.ReadLineAsync(cancellationToken).ConfigureAwait(false)) is not null)
                {
                    files.Add(line);
                }

                onFilesReceived(files);
            }
            catch (Exception ex) when (ex is IOException or ObjectDisposedException)
            {
                // A client disconnected mid-write, or the app is shutting down under us —
                // either way, loop back and wait for the next launch rather than taking
                // the whole listener down over one bad connection.
            }
            catch (OperationCanceledException)
            {
                break;
            }
        }
    }

    public void Dispose()
    {
        _listenerCts?.Cancel();
        _listenerCts?.Dispose();

        if (IsFirstInstance)
        {
            _mutex.ReleaseMutex();
        }

        _mutex.Dispose();
    }
}
