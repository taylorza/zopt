# ZOPT

A peephole optimizer for the ZNC Compiler

## Rule File Syntax

Each rule is made up of three sections, one of which (`constraints:`) is optional. A short comment header is recommended but not required.

```text
# Rule: <short description>
pattern:
  <assembly lines to match>
constraints:
  <RPN expression — optional>
replacement:
  <assembly lines to emit>
```

1. **`pattern:`** — The sequence of assembly lines to match in the input.
2. **`constraints:`** — An optional RPN expression that must evaluate to non-zero for the rule to fire. Must appear before `replacement:`.
3. **`replacement:`** — The lines to emit in place of the matched pattern. Use a single `-` to delete the matched lines entirely.

## Placeholders

Patterns may include up to 10 placeholders (`$1` through `$9`, and `$0`). A placeholder captures whatever token or operand appears at that position in the matched source line and can be reused in the `constraints:` and `replacement:` sections.

Placeholders serve two purposes:

- **Capturing operands** to reuse in the replacement or to test in constraints.
- **Enforcing consistency** — if the same placeholder appears more than once in a pattern, all occurrences must match the same text.

Example — rewrite a redundant two-register load sequence:

```plaintext
pattern:
  ld hl,$1
  push hl
  ld hl,$2
  pop de
replacement:
  ld de,$1
  ld hl,$2
```

## Deleting Code

To remove a matched sequence entirely, use `-` as the sole line under `replacement:`:

```plaintext
# Rule: Remove redundant exchange
pattern:
  push hl
  pop de
  ex de,hl
replacement:
-
```

## Expressions and RPN

Both `constraints:` and `$eval(...)` in replacement lines use the same expression language written in **Reverse Polish Notation (RPN)**: operands come first, then the operator that consumes them. Parentheses are accepted as visual aids but have no effect on evaluation.

| Infix | RPN equivalent |
|-------|----------------|
| `$1 + $2` | `$1 $2 +` |
| `($1 + 3) * 4` | `$1 3 + 4 *` |
| `$1 >= 0 and $1 <= 255` | `$1 0 >= $1 255 <= and` |

### Operator reference

#### Arithmetic (integer operands)

| Operator | Description |
|----------|-------------|
| `+` | Addition |
| `-` | Subtraction |
| `*` | Multiplication |
| `/` | Integer division |
| `%` | Modulo (remainder) |

#### Comparison

Return 1 (true) or 0 (false). Work on integers or strings.

| Operator | Description |
|----------|-------------|
| `<` | Less than |
| `>` | Greater than |
| `<=` | Less than or equal |
| `>=` | Greater than or equal |
| `=` | Equal |
| `<>` | Not equal |

#### Logical (integer operands)

| Operator | Description |
|----------|-------------|
| `and` | Logical AND (non-zero = true) |
| `or` | Logical OR |
| `xor` | Logical XOR |

#### Bitwise (integer operands)

| Operator | Description |
|----------|-------------|
| `band` | Bitwise AND |
| `bor` | Bitwise OR |
| `bxor` | Bitwise XOR |
| `shl` | Shift left |
| `shr` | Shift right (arithmetic) |

#### String / value operators

| Operator | Operands | Description |
|----------|----------|-------------|
| `isnumeric` | value | 1 if value is a numeric constant (decimal, `0x` hex, or `$`-prefixed Z80 hex); 0 otherwise |
| `startswith` | string prefix | 1 if string begins with prefix |

## Constraints

The `constraints:` block holds a single RPN expression. If it evaluates to zero the rule is skipped; otherwise the replacement is applied. The block must appear **before** `replacement:`.

Example — require `$1` is a numeric constant:

```plaintext
constraints:
  ($1 isnumeric)
```

Example — require `$1` is a port number in the range 0..255:

```plaintext
constraints:
  ($1 isnumeric) ($1 0 >=) and ($1 255 <=) and
```

Example — require `$1` is numeric or is a compiler-generated label (starts with `_`):

```plaintext
constraints:
  ($1 isnumeric) ($1 '_' startswith) or
```

*Note:* Parentheses in constraints serve to clarify grouping; they are not required for evaluation.

## Computing Values in Replacements with `$eval`

Use `$eval(RPN_expression)` inside a replacement line when the optimizer needs to compute a numeric value from captured placeholders. The result is written as a decimal integer into the output.

```text
$eval(<RPN expression>)
```

Always guard `$eval` with an `isnumeric` constraint on every placeholder used inside it.

### Examples

**Optimize Zeroing the A Register**

```plaintext
pattern:
  ld a,0
replacement:
  xor a
```

**Remove an Unnecessary Jump to the Immediate Next Line**

```plaintext
pattern:
  jp $1
$1
replacement:
$1
```

`$1` captures the target label on the first line and verifies the very next line carries that same label. The jump is removed; the label is kept because other code may branch to it.

**Constant Folding**

A one-pass compiler may add two constants through the stack. This rule folds them at optimize time:

```plaintext
  ld hl,4
  push hl
  ld hl,3
  pop de
  add hl,de
```

```plaintext
pattern:
  ld hl,$1
  push hl
  ld hl,$2
  pop de
  add hl,de
constraints:
  ($1 isnumeric) ($2 isnumeric) and
replacement:
  ld hl,$eval($1 $2 +)
```

With `$1 = 4` and `$2 = 3` the optimizer emits `ld hl,7`.

**Split a 16-bit Constant into High and Low Bytes**

```plaintext
pattern:
  ld hl,$1
  ld $2,h
  ld $3,l
constraints:
  ($1 isnumeric)
replacement:
  ld $2,$eval($1 8 shr 255 band)
  ld $3,$eval($1 255 band)
```

`$eval($1 8 shr 255 band)` shifts right by 8 then masks, giving the high byte. `$eval($1 255 band)` masks the low byte directly.

## Multiple Patterns and Variants

The same logical optimization can appear in different generated forms. Write a separate `pattern:`/`replacement:` block for each variant rather than one overly-broad rule. The optimizer tries rules in file order, so place more-specific rules first.

Example — increment by 1 appears in two common forms:

```plaintext
pattern:
  ld de,$1
  ld hl,1
  add hl,de
replacement:
  inc hl

pattern:
  push hl
  ld hl,1
  pop de
  add hl,de
replacement:
  inc hl
```

## Best Practices

- Use a `# Rule:` comment header on every rule.
- Prefer narrow, specific patterns over broad ones with many placeholders.
- Guard any `$eval` and numeric operations with `isnumeric` constraints.
- Ensure replacements preserve all flags and register values that the surrounding code depends on.
- Add one rule at a time and verify it before adding more.
- Place more-specific rules earlier in the file so they fire before related catch-all rules.

## Common Pitfalls

- Placeholders that are too broad and match instructions not intended.
- Replacements that clobber flags or auxiliary registers the surrounding code relies on.
- Using `$eval` on a placeholder without a matching `isnumeric` constraint.
- Assuming a particular register state in the replacement that the pattern does not guarantee.

## Rule Templates

**Delete a matched sequence:**

```plaintext
# Rule: <short description>
pattern:
  <line 1>
  <line 2>
replacement:
-
```

**Rewrite using captured operands:**

```plaintext
# Rule: <short description>
pattern:
  <instr> $1,$2
  <instr> $3
constraints:
  ($1 isnumeric)
replacement:
  <instr> $1
  <other> $2,$3
```

**Rewrite with a computed constant:**

```plaintext
# Rule: <short description>
pattern:
  <instr> $1
  <instr> $2
constraints:
  ($1 isnumeric) ($2 isnumeric) and
replacement:
  <instr> $eval($1 $2 +)
```

## Quick Reference

| Element | Description |
|---------|-------------|
| `# Rule: ...` | Human-readable header (optional, recommended) |
| `pattern:` | Assembly lines to match |
| `constraints:` | Optional RPN guard; must precede `replacement:` |
| `replacement:` | Lines to emit; `-` alone deletes all matched lines |
| `$1` … `$9` | Placeholders: capture operands in pattern, expand in replacement |
| `$eval(expr)` | Evaluate an RPN expression and insert the integer result |
| `isnumeric` | 1 if operand is a numeric constant, 0 otherwise |
| `startswith` | 1 if string (left operand) begins with prefix (right operand) |