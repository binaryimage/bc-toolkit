#!/usr/bin/env bash
# Downloads open-licence (OFL) display fonts from Google Fonts into the APK's assets.
# A font that fails to download is skipped; the app also has Android's built-in families.
set -u
OUT="$1"; mkdir -p "$OUT"
FAMILIES=(
  "Cinzel" "Cinzel Decorative" "Playfair Display" "Bodoni Moda" "Cormorant Garamond" "Abril Fatface"
  "Marcellus" "Forum" "Italiana" "Julius Sans One" "Poiret One" "Josefin Sans" "Orbitron" "Audiowide"
  "Michroma" "Syncopate" "Zen Dots" "Quantico" "Rajdhani" "Teko" "Oswald" "Bebas Neue" "Anton"
  "Righteous" "Monoton" "Limelight" "Bungee" "Press Start 2P" "VT323" "Share Tech Mono"
  "Major Mono Display" "Silkscreen" "Great Vibes" "Pinyon Script" "Dancing Script" "Satisfy" "Pacifico"
  "Lobster" "Yellowtail" "UnifrakturMaguntia" "Pirata One" "Creepster" "Nosifer" "Megrim" "Comfortaa"
  "Exo 2" "Kanit" "Federo" "Black Ops One" "Staatliches" "Gruppo" "DM Serif Display" "Shrikhand"
  "Rubik Mono One" "Wallpoet" "Faster One" "Codystar" "Fredericka the Great" "Rye" "Diplomata"
  "Metal Mania" "New Rocker" "Iceland" "Turret Road" "Space Grotesk" "Bowlby One SC" "Sancreek" "Ewert"
)
ok=0; bad=0
for fam in "${FAMILIES[@]}"; do
  q="${fam// /+}"
  css=$(curl -sS --max-time 20 "https://fonts.googleapis.com/css2?family=$q" || true)
  url=$(printf '%s' "$css" | grep -o 'https://fonts.gstatic.com/[^)]*' | head -1)
  if [ -z "$url" ]; then echo "no url: $fam"; bad=$((bad+1)); continue; fi
  f="$OUT/$fam.ttf"
  curl -sS --max-time 30 -o "$f" "$url" || { rm -f "$f"; bad=$((bad+1)); continue; }
  magic=$(head -c 4 "$f" | od -An -tx1 | tr -d ' \n')
  case "$magic" in
    00010000|4f54544f|74727565) ok=$((ok+1)) ;;
    *) echo "not a ttf ($magic): $fam"; rm -f "$f"; bad=$((bad+1)) ;;
  esac
done
echo "fonts: $ok downloaded, $bad skipped"
du -sh "$OUT"
