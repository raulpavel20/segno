# Project documentation

These documents define what this project is and the direction it is taking. They also record each decision and why it was made. Read them before working on the code. Keep them current: when a decision changes, the docs change in the same commit.

> Working name: **foss-daw**. The product name is still to be decided (see [ADR 0008](decisions/0008-naming-and-trade-dress.md)).

## Start here

| Document | What it answers |
|---|---|
| [vision.md](vision.md) | What we are building, who it is for, what defines it, and what we are deliberately *not* building. |
| [design/ui-principles.md](design/ui-principles.md) | What "modern UI" means here, in concrete and checkable terms. |
| [design/design-language.md](design/design-language.md) | The chosen theme ("Steel"): every colour, size and type value. |
| [design/daw-shell.md](design/daw-shell.md) | How the DAW shell is built: code layout, threads, layout, actions and shortcuts, timeline. |
| [devices/device-suite.md](devices/device-suite.md) | The full set of instruments, effects and modulators: Live Suite's coverage as the minimum, best-in-class as the aim. Plus the quality bar every device must meet. |
| [architecture.md](architecture.md) | How the system is structured: modules, the two hosts, state, UI and verification. |
| [roadmap.md](roadmap.md) | The order things are built in and what "done" means for each milestone. |
| [innovation-backlog.md](innovation-backlog.md) | Ideas for going beyond Live in the DAW and across devices, and which are scheduled. |
| [dev-environment.md](dev-environment.md) | The reference test machine, the Linux audio setup for testing by ear, and the toolchain install. |
| [open-questions.md](open-questions.md) | Decisions still to be made, and which milestone each one blocks. |
| [decisions/](decisions/README.md) | Architecture Decision Records (ADRs): one file per decision, including the context and trade-offs. |

## How these docs are maintained

- **Vision, UI principles and device suite** are product documents. They change only when the direction changes, and such a change is recorded in an ADR.
- **ADRs are never rewritten after acceptance.** To change a decision, write a new ADR that *supersedes* the old one and mark the old one `Superseded by NNNN`.
- **The roadmap** is a living document. Milestone status is updated at the end of every working session.
- **Device specs** (`devices/<name>/SPEC.md`, created when a device is started) hold each device's parameters, DSP design, test thresholds and A/B listening notes.
- Docs are written in plain Markdown. Diagrams are ASCII or Mermaid so they diff well in git.
