# LogicFilter

## Overview
LogicFilter processes input TDC (Time-to-Digital Converter) data based on a user-defined logical expression. If the expression evaluates to "true" at a specific timestamp, that timestamp is added to the output data.
The processing time unit is four times the unit of the input TDC data. For example, if the input TDC data unit is 1 ns, the output data unit will be 4 ns.

## Redis DB Keys

### Key prefix
- Defaut: parameter:LogicFilger:

### trigger-signals
Defines the input channels. Each input signal is specified as a set: (<group id> <module id> <channel id> <offset> <left width> <right width>). The 4th and 5th paramter are optional.(detail in the section "trigger-width")
Signal IDs: Assigned sequentially starting from 0 based on the order of the definitions.
Capacity: A maximum of 32 signals can be defined.
- Default: (0xc0a802a9 0 0) (0xc0a802a9 1 0)

### trigger-expression
Defines the logical expression applied to the input signals. It supports both Infix notation and RPN (Reverse Polish Notation).

**Operands:**
- Numbers represent the assigned Signal IDs.

**Operators:**
- & : Logical AND
- | : Logical OR
- ! : Logical NOT

**Syntax:**
- Infix: Use parentheses ( and ) for order of operations.
- RPN: Must be prefixed with the keyword RPN.

- Default: RPN 0 1 &  
(Generates a trigger based on the coincidence of input channels 0 and 1.)

### trigger-width
Sets the TDC coincidence window. Let $T$ be the trigger-width value and $t$ be the TDC value; the window is defined as: 
$$[t - T/2, t + T/2]$$
When user gives trigger-signals with 3 parameters(, which are <module id>, <channel id>, and <offset>), the TDC coincidence window is defined as above.
When user gives trigger-signals with 5 parameters(, which are <module id>, <channel id>, <offset>, <left width>, and <right width>), the TDC coincidence window is defined as $$[t - leftwidth, t+ rightwidth]$$
The time unit for $T$ is four times the time unit of the input TDC data.
- Default: 10

# Still in Experimental
## Extended capacity
We can now support trigger logic with more than 32 channels.

## trigger-signals
Defines the input channels. Each input signal is specified as a set: (<Group-Subgroup id> <module id> <channel id> <offset>).
Group IDs: Defines which group and subgroup the signal belongs to.( "Group", and "Subgroup" are described below. )
Capacity: Up to 32 signals can be registered to each Subgroup. Up to 32 groups can be defined.
- Default: `(0 0xc0a802a9 0 0) (1 0xc0a802a9 1 0)`

### Group and Subgroup
A **Group** represents one logical unit used in `trigger-expression`. A Group can contain one or more signals and **Subgroups**. A **Subgroup** can contain at least two signals.
- Signals in the same Subgroup are combined with logical AND.
- Signals and Subgroups in the same Group are combined with logical OR.

## trigger-expression
Defines the logical expression applied to the groups. It supports both Infix notation and RPN (Reverse Polish Notation).
- Default: `0 & 1`

### Compatible expressions
With `trigger-signals` and `trigger-expression`, 
```(0 fem0 ch0 offs0) (1 fem1 ch1 offs1) (2-0 fem2 ch2 offs2) (2-0 fem3 ch3 offs3) (2-0 fem4 ch4 offs4) (2 fem5 ch5 offs5) (2 fem6 ch6 offs6)```,
```(0 & 1) & 2```, it has a expression, being equivalent. That is
```(0 fem0 ch0 offs0) (1 fem1 ch1 offs1) (2 fem2 ch2 offs2) (3 fem3 ch3 offs3) (4 fem4 ch4 offs4) (5 fem5 ch5 offs5) (6 fem6 ch6 offs6)```,
```(0 & 1) & ((2 & 3 & 4) | (5 & 6))```.

### Drawback of using Subgroups
Computational cost depends on number of Subgroups. If your trigger logic has less than 32 channels, it is recommended to use without subgroups.


### 従来との違い・互換性
- "trigger-signals"は(group IP CH Offset left_width right_width)もしくは、(“Group-Subgroup” IP CH Offset left_width right_width)を列挙することになります
- 同じグループに属するチャンネル/サブグループはORのロジックが自動で組まれます
- 同じサブグループに属するチャンネルはANDのロジックが自動で組まれます
- "trigger-expression"はこれまでどおりの記法だが、グループ同士のロジックを記述する

## Independent multi-trigger definitions (this branch)

This branch removes OR-term flagging. Each explicit trigger ID owns a before
bit (0..11) and an after-prescale bit (12..23) in `TrgTime::type`.
The upper byte remains `0xaa`.
The serialized `TrgTime` size/layout is unchanged, but flag semantics differ
from the OR-flags branch. Do not interpret old files with the new ID mapping.

Use decimal trigger IDs 0..11, separated by a newline or semicolon in the SAME
Redis `trigger-expression` value. Group IDs inside formulas still refer to
`trigger-signals`. IDs may be sparse and line order does not assign IDs.

```bash
# Example: group 0=beam, 1=kaon, 2=interaction.
expressions='0: 0
1: 0 & 1
2: 0 & 2
3: 0 & 1 & 2'
redis-cli -u "$server" HSET parameters:LogicFilter trigger-expression "$expressions"
```

Repeated HSET commands to the SAME field overwrite it; build the complete
multiline value first and write once. A compact equivalent is:

```text
0: 0; 1: 0 & 1; 2: 0 & 2; 3: 0 & 1 & 2
```

Each expression accepts infix `!`, `&`, `|`, parentheses (precedence in that
order), or an explicit `RPN` / `rpn` prefix, e.g. `3: RPN 0 1 & 2 &`.
Duplicate IDs, IDs >=12, empty/malformed expressions, and unsupported tokens
are rejected during initialization. Decimal group IDs must be 0..31.

All expressions are evaluated during initialization into ONE uint32_t LUT.
An entry is a bitmask of the independent triggers true for that group pattern.
There is no per-trigger TF scan, no OR decomposition and no OR-flags LUT.
The table size still grows as 2^(highest referenced group ID + 1), so syntactic
support for 32 groups is not a guarantee that a large dense table fits in RAM.
Registered groups above that highest referenced ID are masked out for lookup;
they do not veto a trigger that does not use them.

At runtime, each trigger is edge-detected independently using:

```cpp
const uint32_t rising = currentTriggerFlags & ~previousTriggerFlags;
```

If multiple IDs rise in the SAME time bin, ONE record is emitted with all
their bits set. A new trigger can fire while another stays true. Flags mean
newly firing IDs, not all conditions currently true. Example:

- beam rises alone: `type=0xaa000001`;
- kaon rises during beam: `type=0xaa000002`;
- interaction rises during beam+kaon: `type=0xaa00000c` (IDs 2 and 3);
- all three rise simultaneously: `type=0xaa00000f`.

A legacy single expression without IDs is accepted as trigger ID 0. For
`(0 & 1) | (2 & 3)`, the entire expression is ONE trigger: when the second OR
arm rises while the first remains true, no new trigger is emitted. Define
separate trigger IDs if independent edges are required.

HBF boundary behavior is unchanged: bin 0 is the reference state; comparisons
start at bins 0->1. No bin-0 trigger or cross-HBF state is added. Window/offset
and two-input subgroup behavior are unchanged.

### Periodic prescaling

Use `ID-N: expression` to accept every N-th rising edge of that ID. `ID: expression`
defaults to N=1. N=0 disables acceptance by that ID but keeps its raw truth bit
and before-prescale count. N must be a decimal uint32_t (0..4294967295).

```bash
expressions='0-100: 0 & 1
1-10: 0 & 1 & !3
2-1: 0 & 1 & !3 & 2
3-1: 0 & 1 & !3 & 2 & 4'
redis-cli -u "$server" HSET parameters:LogicFilter trigger-expression "$expressions"
```

Each ID has an independent phase counter. The first acceptance is its N-th
edge, not its first edge. Phases persist across HBFs and TFs, including empty
ones, and reset in LogicFilter::PreRun (and after successful MakeTable).
Settings are loaded during initialization, not changed live in RUNNING.

If any ID accepts a time bin, ONE TrgTime is emitted. Bits 0..11 contain ALL
simultaneously rising IDs BEFORE prescale, including N=0 IDs. Bits 12..23
contain the accepted IDs, shifted by 12. For example, raw=0x3 and
accepted=0x2 emits type=0xaa002003. If no ID accepts, no record is emitted;
rejected-only edges are counted but not saved as TrgTime records.
GetTrgTimeBeforeFlags(type) and GetTrgTimeAfterFlags(type) return unshifted
12-bit masks. GetTrgTimeTriggerFlags(type) still returns the full 24-bit mask.
The binary record layout is unchanged, but flag interpretation has changed:
old files must not be interpreted using this new mapping. These are exact-bin rising flags, not a new
physical-event association window or a mask of continuously active conditions.

Before/after uint64_t counts are available via GetBeforeCounts()/GetAfterCounts()
on Trigger and TriggerMap. Before counts include disabled IDs; after counts
count accepted IDs, not merged records. Counts/phases are per worker, reset per
run, and are NOT yet exported to Redis or a scaler file. This is periodic
prescaling local to each process, not globally ordered every-N-th sampling;
periodic input and TF distribution may correlate with sampling. No spill reset
is added. Spill-gate selection is described below. Configuration logs include each ID's factor.

The public accessors are now `GetHitTriggerFlags()`, `LookUpTriggerFlags()` and
`Filter::GetTrgTimeTriggerFlags()`. Old OR-named accessors are removed.
`MatchesTrgTimeType()` retains its existing wildcard/exact-match behavior.

### Spill-gated trigger IDs

Use `ID-prescale-gate: expression`, where gate=0 disables the ID, gate=1
enables only spill ON, gate=2 only OFF, and gate=3 both. Omitted gate defaults
to 3; omitted prescale defaults to 1. Legacy single expressions still work.
Example: `0-1000-1: 0 & 1; 4-1-2: 5 & 6`.

`spill-gate-bit` selects HBD1 hbflag bit 0..15 (default 0, as requested).
`spill-gate-active-high=true` (default) means bit=1 is ON; false reverses it.
Use bit=-1 to force unknown. Options are loaded during task initialization.

Modern femType 5/6 HBD1 records are read from normal delimiter/HBF-part edges;
legacy/unsupported forms are not interpreted as modern frame flags.
For split data/delimiter parts, the following delimiter is inspected before
processing the data. Frame numbers and states must agree across observed HBDs.
No evidence, conflicting states or mismatched frame numbers result in Unknown.
Unknown deliberately selects ON IDs, not OFF IDs. Remaining valid evidence
can be used when one FE has no readable HBD. Unusual unsupported layouts may
therefore select ON as Unknown; validate actual HBD coverage before physics use.

Selection is applied AFTER logical rising-edge detection and BEFORE prescale.
Disabled IDs do not increment before/after counts or advance prescale phases.
Before flags mean gate-selected, pre-prescale rising IDs. The existing 12+12
flag layout and 8-byte TrgTime record are unchanged. No trigger is synthesized
on a gate transition, and no cross-HBF trigger state or spill reset is added.
Gate state is HBF-granular. Counts of ON/OFF/Unknown/conflicting processed HBF
data slots are logged every 5 seconds and at PostRun; Unknown is not silently
reported as a measured ON. The synthetic E80 file has all flag bits zero and
will appear OFF with bit0/active-high; it does not simulate real spill gates.
