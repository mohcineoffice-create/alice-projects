#!/bin/sh
# Construit la version de démonstration statique (Vercel) à partir du frontend app/.
# Usage : sh static-demo/build.sh  (depuis im-jx-smart-suite/)
set -e
OUT=static-demo/dist
rm -rf "$OUT" && mkdir -p "$OUT"
cp -r app/. "$OUT"/
cp static-demo/demo-shim.js "$OUT/shared/demo-shim.js"
gzip -9 -c static-demo/demo-data.json > "$OUT/demo-data.json.gz"
# Bibliothèques tierces servies depuis cdnjs (déploiement plus léger)
rm -rf "$OUT/shared/vendor"
# Le shim doit être chargé avant tout autre script.
find "$OUT" -name "*.html" | while read -r f; do
  sed -i '0,/<head>/s//<head>\n<script src="\/shared\/demo-shim.js"><\/script>/' "$f"
  sed -i \
    -e 's#/shared/vendor/qrious.min.js#https://cdnjs.cloudflare.com/ajax/libs/qrious/4.0.2/qrious.min.js#' \
    -e 's#/shared/vendor/jspdf.umd.min.js#https://cdnjs.cloudflare.com/ajax/libs/jspdf/2.5.1/jspdf.umd.min.js#' \
    -e 's#/shared/vendor/JsBarcode.all.min.js#https://cdnjs.cloudflare.com/ajax/libs/jsbarcode/3.11.5/JsBarcode.all.min.js#' \
    -e 's#/shared/vendor/html5-qrcode.min.js#https://cdnjs.cloudflare.com/ajax/libs/html5-qrcode/2.3.8/html5-qrcode.min.js#' "$f"
done
cat > "$OUT/index.html" <<'HTML'
<!doctype html><html lang="fr"><head><meta charset="utf-8"><meta http-equiv="refresh" content="0;url=/login.html"><title>IM JX Smart Suite — Démo</title></head><body></body></html>
HTML
echo "OK -> $OUT"
