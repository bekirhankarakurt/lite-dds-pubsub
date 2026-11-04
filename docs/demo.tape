# Re-record with: vhs docs/demo.tape   (run from the repository root after building into ./build)
Output docs/demo.gif

Set Shell "bash"
Set FontSize 15
Set Width 1180
Set Height 640
Set Padding 16
Set Framerate 12
Set TypingSpeed 35ms
Set Theme "Dracula"

Hide
Type "export PS1='$ ' && clear" Enter
Show

Type "./build/bin/logger_subscriber --count 6 &" Enter
Sleep 1s
Type "./build/bin/sensor_publisher --count 6 --rate 5" Enter
Sleep 3s
Type "wait" Enter
Sleep 1s
Type "./build/bin/lossy_link_demo --loss 40" Enter
Sleep 4s
