#!/bin/bash
echo "=== LANE-18 MUTASYON BATARYASI SAYIMI (ham loglardan) ==="
all=$(cat mutations_hdr.log mut_n.log mut_n2.log mut_q.log mut_r.log mut_m05.log)
# her deneme: 'MUT[ID]' ... 'rc=' veya 'BUILD-FAIL'/'LINK-FAIL' karar satiri
printf '%s\n' "$all" | awk '
/^MUT\[/ { id=$0; gsub(/^MUT\[|\]$/,"",id); id=id" "; buf=id; next }
{ buf=buf $0 "\n" }
/rc=0  !! YESIL KALDI/ { res[id]="KACIRDI"; next }
/BUILD-FAIL|LINK-FAIL|apply_mut.py: |MUT-FAIL/ { if (res[id]=="") res[id]="GECERSIZ"; next }
/rc=1  OK  -> KIRMIZI/ { res[id]="YAKALADI"; next }
END { for (k in res) { c++; if (res[k]=="YAKALADI") y++; else if (res[k]=="KACIRDI") e++; else g++ }
     printf "gecerli mutasyon sayisi : %d\n", c
     printf "  YAKALADI (kirmizi)     : %d\n", y+0
     printf "  KACIRDI  (yesil kaldi): %d\n", e+0
     printf "  gecersiz/derleme-hatasi: %d\n", g+0
     printf "KACIRANLAR: "
     for (k in res) if (res[k]=="KACIRDI") printf "%s ", k
     printf "\nYAKALAYANLAR: "
     for (k in res) if (res[k]=="YAKALADI") printf "%s ", k
     printf "\n" }'
echo
echo "=== tr_coverage KAPISI (ayri sayim) ==="
for x in T00 T02 T03 T04 T05c T06; do printf '%-5s ' "$x"; done; echo
