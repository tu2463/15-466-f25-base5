# Load .blend file

/Applications/Blender.app/Contents/MacOS/Blender -y --background --python scenes/export-meshes.py -- scenes/room.blend:Main dist/room.pnct
/Applications/Blender.app/Contents/MacOS/Blender -y --background --python scenes/export-scene.py -- scenes/room.blend:Main dist/room.scene