! A very simple expression language to serve as a testbed for all
! compiler stages from lexing to code generation

$decimal_literal := $digit $digit*

$digit := 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9

$mul_op := @*

$add_op := +
