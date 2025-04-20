# ZOPT

A peephole optimizer for the ZNC Compiler

## Rule File Syntax

Rules consist of three sections, one of which (the **constraints** section) is optional:

1. **Pattern Section:**  
   Introduced by the keyword `pattern:`, this section is followed by the code sequence you wish to match.

2. **Constraints Section (Optional):**  
   Introduced by the keyword `constraints:`, this section lets you specify additional requirements for the matched pattern. For example, you may require that one of the captured variables (discussed later) is a numeric constant.

3. **Replacements Section:**  
   Introduced by the keyword `replacements:`, the lines following this section will replace the lines that were matched.

## Variables

Patterns may include literal strings and up to 10 variables (labeled `$0` through `$9`). These variables serve two purposes:

- **Capturing Code:**  
  They capture portions of the matched pattern to be reused in the replacement text.

- **Ensuring Consistency:**  
  They help ensure that repeated parts of the matched pattern have the same values.

We'll explore these with examples later.

## Expressions

Expressions are especially useful in two cases:

1. **Defining Constraints:**  
   Use expressions to specify additional rules that the matched pattern must satisfy.

2. **Calculating New Values:**  
   Use expressions in the replacement text to compute values dynamically.

To keep the optimizer simple, expressions use **Reverse Polish Notation (RPN)**. In RPN, operands are provided first, followed by the operator. For instance:

- The infix expression `2 * 3` is written as:

`2 3 *`

- The infix expression `(2 + 3) * 4` is written as:

`2 3 + 4 *`


*Note:* Parentheses may be included purely as visual aids; they are not required for computation.

## Examples

### Optimize Zeroing the A Register

This rule replaces any occurrence of `ld a,0` with `xor a`:

```plaintext
pattern:
  ld a,0
replacement:
  xor a
```

### Remove Unnecessary Jump to the Immediate Next Line

A basic one-pass compiler may emit a jump instruction even when its target is the very next line. This rule removes such redundant jumps:

```plaintext
pattern:
  jp $1
$1
replacement:
  $1
```

In this rule:
* The first occurrence of $1 captures the target label.
* The second occurrence ensures that the line immediately following the jump has the same label as captured.
* The replacement simply removes the jump while preserving the label, as other parts of the code might jump to it.

### Constant Folding
This example optimizes code that adds two constants using the stack. Consider the following code:

```asm
  ld hl,4
  push hl
  ld hl,3
  pop de
  add hl,de
```

The following rule captures this pattern and uses constraints to ensure that both captured values are numeric:

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
  ld hl, $eval($1 $2 +)
```

Here:
* The constraint verifies that both $1 and $2 are numeric.
* The $eval function computes the sum of $1 and $2.
* Hence, the original code is optimized to:

```asm
  ld hl,7
```

**Note:** The parentheses in the constraints section serve to clarify the grouping of conditions; they are not technically required.