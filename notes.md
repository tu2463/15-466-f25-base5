# Load .blend file

/Applications/Blender.app/Contents/MacOS/Blender -y --background --python scenes/export-meshes.py -- scenes/room.blend:Main dist/room.pnct
/Applications/Blender.app/Contents/MacOS/Blender -y --background --python scenes/export-scene.py -- scenes/room.blend:Main dist/room.scene

node Maekfile.js

node Maekfile.js && ./dist/server 30000

./dist/server 30000

./dist/client localhost 30000
