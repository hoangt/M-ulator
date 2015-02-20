#!/bin/bash

# Travis CI doesn't support fuse, so instead we use user mode Linux to put some
# Linux in our sandboxed Linux and get things working, from:
#  http://stackoverflow.com/questions/23937413/continuous-delivery-for-fuse-oss-and-github/28070347#28070347

CURDIR="`pwd`"

cat > umltest.inner.sh <<EOF
#!/bin/sh
(
   export PATH="$PATH"
   set -e
   set -x
   insmod /usr/lib/uml/modules/\`uname -r\`/kernel/fs/fuse/fuse.ko
   cd "$CURDIR"
   ./tests.sh
   echo Success
)
echo "\$?" > "$CURDIR"/umltest.status
halt -f
EOF

chmod +x umltest.inner.sh

/usr/bin/linux.uml init=`pwd`/umltest.inner.sh rootfstype=hostfs rw

exit $(<umltest.status)
