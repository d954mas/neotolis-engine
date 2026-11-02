# frame_inspector --serve over a pipe: every answer sits between "> command" and "= ok" or "= error", a bad
# --frame ends the session before it reads a command, and options after a command belong to the command.
# Lines end in \n on every OS, and a blank line gets no answer.
# Run with -DINSPECTOR=<exe> -DCAPTURE=<file> -DWORK=<dir>.
file(WRITE "${WORK}/serve_commands.txt" "groups\nrecord @3x\n\n  \t\nsummary extra\nlist gl_draw_calls\n")
execute_process(COMMAND "${INSPECTOR}" --serve "${CAPTURE}" --frame 51 INPUT_FILE "${WORK}/serve_commands.txt" OUTPUT_VARIABLE out RESULT_VARIABLE rc)
set(want "^> frame 51\nframe 51  # [^\n]*\n= ok\n"
         "> groups\n(#[^\n]*\n)+(g[0-9]+ [^\n]*\n)+total 47 9 67 93952 0\n= ok\n"
         "> record @3x\nerror: [^\n]*\n= error\n"
         "> summary extra\nerror: too many arguments[^\n]*\n= error\n"
         "> list gl_draw_calls\n(@[0-9]+  [^\n]*\n)+records 9\n= ok\n$")
string(CONCAT want ${want})
if(NOT rc EQUAL 0 OR NOT out MATCHES "${want}")
    message(FATAL_ERROR "serve answered (exit ${rc}):\n${out}")
endif()

execute_process(COMMAND "${INSPECTOR}" --serve "${CAPTURE}" --frame 999 INPUT_FILE "${WORK}/serve_commands.txt" OUTPUT_VARIABLE out RESULT_VARIABLE rc)
if(rc EQUAL 0 OR NOT out MATCHES "^> frame 999\nerror: [^\n]*\n= error\n$")
    message(FATAL_ERROR "a bad --frame must end the session (exit ${rc}):\n${out}")
endif()

execute_process(COMMAND "${INSPECTOR}" query "${CAPTURE}" list merged --frame 51 OUTPUT_VARIABLE out RESULT_VARIABLE rc)
if(rc EQUAL 0 OR NOT out MATCHES "^error: list needs a key")
    message(FATAL_ERROR "--frame after a command is the command's argument (exit ${rc}):\n${out}")
endif()
