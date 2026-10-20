# Apple zeigt Gleitkommazahlen mit 16 (double) bzw. 7 (float) Stellen: @(0.1) -> "0.1".
# Darling nimmt 17 bzw. 8 Stellen und zeigt "0.10000000000000001".
s/DBL_DIG \+ 2/DBL_DIG + 1/g
s/FLT_DIG \+ 2/FLT_DIG + 1/g
