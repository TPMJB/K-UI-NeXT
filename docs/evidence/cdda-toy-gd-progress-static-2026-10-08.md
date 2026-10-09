# Toy Commander GD audio progress contract

The driver-admission build could keep a GD audio request processing until
`applied_generation` matched its accepted mailbox generation. Actual audio
application runs only from the game's normal sound update, after its original
updater, on the worker's private stack.

Static review of the exact previously identified Toy executable establishes
that its GD callbacks cannot advance that worker:

| Scalar boundary | Progress available |
|---|---|
| PLAY20 SDK submission at `0x8c0b21a4` | Queues the request, runs one GD EXEC at `0x8c0b225a`, then returns |
| GD status callback `0x8c0b1fd6` | Runs GD EXEC and GETSTAT; no game sound update |
| Blocking SDK wait `0x8c0b2704` | Invokes that GD callback; no sound service |
| Game pause helper `0x8c049cde` | Retries a busy SDK allocation result without invoking GD or sound service |

An active PLAY20 SDK object can cause the pause helper's allocation to remain
busy. Holding the GD request open for a frame-dependent sound application
therefore creates a circular wait. The SDK retains its own request ownership;
GD GETSTAT must still consume a completed or failed result through the normal
CHECK path. The reader must not discard that ownership to accept a new request.
The SDK also registers a separate GD status callback that runs EXEC and
GETSTAT; acknowledgement lets that existing callback retire the request
without invoking sound work from it.

The corrected policy acknowledges accepted audio mailbox work on the first
GD EXEC, independently of hardware application. A missing worker snapshot,
an already known fault, or a generation mismatch remains a terminal error.
`applied_generation` is not changed by acknowledgement. SDK/AICA work is not
moved into the masked GD stack or its polling callbacks.

The host regression submits each of the six audio commands through the real
GD service, leaves worker application behind, executes the production
acknowledgement, and consumes its result through CHECK. It also checks failure,
abort and terminal-handle ownership. This tests command progress without
requiring a frame callback.
It passes with address and undefined-behavior sanitizers. The SH build and
linked fit, integer-instruction and pure masked-mailbox audits also pass.

The reported video shows a fixed No Cliche intro logo, with repeating audio
and no controller response or failure screen. It supplies no program counter
or visible build ID. The static review proves a deadlock in the prior policy;
it does not identify the exact instruction reached in that recording. The
game SDK's separate unbounded G2 waits and FMV performance remain unresolved.

Only authored explanations and scalar contracts are included here. The game
executable, sound driver, decoded instruction listings and recording are not
part of the source or package.
