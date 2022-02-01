# aclocal
# autoconf
# automake --add-missing

mkdir -p config m4
autoreconf --force --install -I config -I m4
