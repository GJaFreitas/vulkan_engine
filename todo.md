# TODO LIST

[X] Query performance counter;

[X] move the file reading function from vulkan to the base layer;

[X] have a file with all the global variables and be able to modify it on the fly with hot reloading kind of thing;

[X] create logging functionality;

[X] Change all allocations in gltf loading to be arenas

[X] Make pipeline creation more modular pipelineCreate(PipelineCreationInfo info)?

[ ] Create a keybind load and write function that keeps track of bound keys.

[X] UI pipeline

[X] Menu/console for messing with stuff

[ ] Map editor mode

[X] Fix antialiasing in font rendering

[ ] Implement indirect drawing

[ ] Set a g_frame_arena variable to make frame allocations easier

### --- STUFF THAT DOESNT WORK ---

Change strViewChr() to strViewUpToChar()

### --- IDEAS FOR LATER THAT I CANT IMPLEMENT NOW ---

outfits should be customizable with an unlock like a spell for doing it
re-enactement should work in cutscenes

Entities dont have to be updated every frame if they are no close to the player.
