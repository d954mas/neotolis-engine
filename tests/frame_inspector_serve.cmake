# frame_inspector --serve over a pipe: every answer sits between "> command" and "= ok" or "= error", a bad
# --frame ends the session before it reads a command, and options after a command belong to the command.
# Lines end in \n on every OS, a blank line gets no answer, and a line too long to read gets an error even
# when what fits of it is blank.
# Run with -DINSPECTOR=<exe> -DCAPTURE=<file> -DWORK=<dir>.
string(REPEAT " " 1030 spaces)
file(WRITE "${WORK}/serve_commands.txt" "groups\nrecord @3x\n\n  \t\nsummary extra\n${spaces}summary\nlist gl_draw_calls\n")
execute_process(COMMAND "${INSPECTOR}" --serve "${CAPTURE}" --frame 0 INPUT_FILE "${WORK}/serve_commands.txt" OUTPUT_VARIABLE out RESULT_VARIABLE rc)
set(want "^> frame 0\nframe 0  # sequence 999, [^\n]*\n= ok\n"
         "> groups\n(#[^\n]*\n)+(g[0-9]+ [^\n]*\n)+total 47 9 67 93952 0\n= ok\n"
         "> record @3x\nerror: [^\n]*\n= error\n"
         "> summary extra\nerror: too many arguments[^\n]*\n= error\n"
         "> +\nerror: the line is longer than 1023 characters\n= error\n"
         "> list gl_draw_calls\n(@[0-9]+  [^\n]*\n)+records 9\n= ok\n$")
string(CONCAT want ${want})
if(NOT rc EQUAL 0 OR NOT out MATCHES "${want}")
    message(FATAL_ERROR "serve answered (exit ${rc}):\n${out}")
endif()

execute_process(COMMAND "${INSPECTOR}" --serve "${CAPTURE}" --frame 999 INPUT_FILE "${WORK}/serve_commands.txt" OUTPUT_VARIABLE out RESULT_VARIABLE rc)
if(rc EQUAL 0 OR NOT out MATCHES "^> frame 999\nerror: [^\n]*\n= error\n$")
    message(FATAL_ERROR "a bad --frame must end the session (exit ${rc}):\n${out}")
endif()

execute_process(COMMAND "${INSPECTOR}" query "${CAPTURE}" list merged --frame 0 OUTPUT_VARIABLE out RESULT_VARIABLE rc)
if(rc EQUAL 0 OR NOT out MATCHES "^error: list needs a key")
    message(FATAL_ERROR "--frame after a command is the command's argument (exit ${rc}):\n${out}")
endif()
