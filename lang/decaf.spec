! Conventions:
! !    starts a comment till the end of the line
! $    marks the start of a non-terminal
! @_   marks a literal space
! @@   marks a literal @
! @|   marks a literal |
! @*   marks a literal *
! @$   marks a literal $
! |    separates 2 alternatives
! *    >= 0 instances
!
! Using a special escape character like @ reduces the chance of
! instroducing errors. For example, an expression like a | | c
! might be written with the intension of a OR (| AND c) or there
! might be a missing argument between the 2 |s, i.e. a OR b OR c
!
! Each non-terminal is specified on exactly one line

$res_word := break | callout | class | continue | else | for | if | return | void

$type := int | boolean

$bool_literal := true | false

$assign_op := = | += | -=

$bin_op := $arith_op | $rel_op | $eq_op | $cond_op

$arith_op := + | - | @* | / | %

$rel_op := < | > | <= | >=

$eq_op := == | !=

$cond_op := && | @|@|

$literal := $int_literal | $char_literal | $bool_literal

$id := $alpha $alpha_num*

$alpha_num := $alpha | $digit

$alpha := A | B | C | D | E | F | G | H | I | J | K | L | M | N | O | P | Q | R | S | T | U | V | W | X | Y | Z | a | b | c | d | e | f | g | h | i | j | k | l | m | n | o | p | q | r | s | t | u | v | w | x | y | z | _

$digit := 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9

$hex_digit := $digit | a | b | c | d | e | f | A | B | C | D | E | F

$int_literal := $decimal_literal | $hex_literal

$decimal_literal := $digit $digit*

$hex_literal := 0x $hex_digit $hex_digit*

$char_literal := ' $char '

$string_literal := " $char* "

$char := @@ | ! | # | @$ | % | & | ( | ) | @* | + | , | - | . | / | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | : | ; | < | = | > | ? | @@ | A | B | C | D | E | F | G | H | I | J | K | L | M | N | O | P | Q | R | S | T | U | V | W | X | Y | Z | [ | ] | ^ | _ | ` | a | b | c | d | e | f | g | h | i | j | k | l | m | n | o | p | q | r | s | t | u | v | w | x | y | z | { | @| | } | ~ | \" | \' | \\
