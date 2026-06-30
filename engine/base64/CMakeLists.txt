# nt_base64 -- generic base64 encoder, no engine deps. Devapi capture uses it to put PNG bytes on the
# JSON wire, but it is standalone so any module can encode without pulling devapi.
nt_add_module(nt_base64 LOG_DOMAIN "base64"
    nt_base64.c
    nt_base64.h
)
