BEGIN {
    FS="\t"
}
{
    if ($7 != "P" || ($8 != "PPL" && $8 != "PPLC" && substr($8, 0, 4) != "PPLA") || $15 < 1000) next
    print $9 FS $2 FS $5 FS $6 FS $16 FS $18
}
