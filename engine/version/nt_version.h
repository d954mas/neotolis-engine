#ifndef NT_VERSION_H
#define NT_VERSION_H

/* The engine version: the first 8 hex of the engine commit, "-dirty" appended when tracked
   files differ from it, or "unknown" when the build had no git or the engine is not its own
   repository. Static literal. */
const char *nt_version_engine(void);

#endif // NT_VERSION_H
