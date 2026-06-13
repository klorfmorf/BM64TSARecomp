ARCH=$(uname -m)
LINUX_DEPLOY_ARCH=$(uname -m)

if [ "$ARCH" = "x86_64" ]; then
  ARCH="x86_64"
  LINUX_DEPLOY_ARCH="x86_64"
elif [ "$ARCH" = "aarch64" ]; then
  ARCH="arm_aarch64"
  LINUX_DEPLOY_ARCH="aarch64"
else
  echo "Unsupported architecture: $ARCH"
  exit 1
fi

curl -sSfLO "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$LINUX_DEPLOY_ARCH.AppImage"
curl -sSfLO "https://github.com/linuxdeploy/linuxdeploy-plugin-gtk/raw/master/linuxdeploy-plugin-gtk.sh"

chmod a+x linuxdeploy*
 
mkdir -p AppDir/usr/bin
cp BM64TSARecompiled AppDir/usr/bin/
cp -r assets/ AppDir/usr/bin/
cp recompcontrollerdb.txt AppDir/usr/bin/
cp icons/app.png AppDir/BM64TSARecompiled.png
cp .github/linux/BM64TSARecompiled.desktop AppDir/

"./linuxdeploy-$LINUX_DEPLOY_ARCH.AppImage" --appimage-extract
mv squashfs-root/ deploy
./deploy/AppRun --appdir=AppDir/ -d AppDir/BM64TSARecompiled.desktop -i AppDir/BM64TSARecompiled.png -e AppDir/usr/bin/BM64TSARecompiled --plugin gtk
sed -i 's/exec/#exec/g' AppDir/AppRun
echo 'if [ -f "portable.txt" ]; then' >> AppDir/AppRun
echo '    APP_FOLDER_PATH=$PWD' >> AppDir/AppRun
echo '    cd "$this_dir"/usr/bin/' >> AppDir/AppRun
echo '    APP_FOLDER_PATH=$APP_FOLDER_PATH ./BM64TSARecompiled' >> AppDir/AppRun
echo 'else' >> AppDir/AppRun
echo '    cd "$this_dir"/usr/bin/' >> AppDir/AppRun
echo '    ./BM64TSARecompiled' >> AppDir/AppRun
echo 'fi' >> AppDir/AppRun

# Remove conflicting libraries
rm -rf AppDir/usr/lib/libgmodule*
rm -rf AppDir/usr/lib/gio/modules/*.so
rm -rf AppDir/usr/lib/libwayland*

./deploy/usr/bin/linuxdeploy-plugin-appimage --appdir=AppDir
