# GodotCrowdControl

This plugin adds support for [Crowd Control](https://crowdcontrol.live/) to games made with Godot, letting viewers
change how the game plays on stream.

Documentation: https://developer.crowdcontrol.live/godot/

Requires Godot 4.5 or newer. Implements the Crowd Control 2.0 [PubSub protocol](https://developer.crowdcontrol.live/sockets/)
(PKCE login, no application secret). See [project/README.md](project/README.md) for how to set a game up.

**This does not add effects to already released games.** It is for developers adding Crowd Control to their own game.

## Credits

This plugin was created by [SilicDev](https://github.com/SilicDev) and is continued here from the original
repository at https://github.com/SilicDev/GodotCrowdControl.

Based on https://github.com/nathanfranke/gdextension and the Unity plugin at https://github.com/WarpWorld/crowd-control-unity-plugin

## Known Issues

Only partial C# support.
: Due to several issues in Godot (i.e. no GdExtension virtual functions) full C# support can not be provided. Moving C# to its own GdExtension may make this easier in the future.