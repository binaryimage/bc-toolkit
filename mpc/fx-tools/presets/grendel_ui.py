import sys; sys.path.insert(0, '/home/claude/fx-shared/tools')
import mkui
mkui.write('/home/claude/ref/sdk/plugins/Grendel/ui.json', 'ffd2782d', 'ff120c08', 'ffeee0cc', 'ffa8957f', [
 ("GRENDEL", ["Tape Stop:switch", "Rewind:switch", "Possessed", "Grid:menu",
              "Stop Time", "Start Time", "Tape Eat", "Dropouts",
              "Mix:fader", "Output:fader", None, None]),
 ("TAPE", ["Drive", "Age", "Tone:bi", "Hiss",
           "Wow", "Wow Rate", "Flutter", "Width",
           "Crush", None, None, None]),
])
