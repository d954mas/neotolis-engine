#ifndef NT_VERSION_H
#define NT_VERSION_H

/* "0.1.0+<first 8 hex of the engine commit>[-dirty]", or "0.1.0+unknown" when the build
   had no git or the engine is not its own repository. Static literal. */
const char *nt_version_engine(void);

#endif // NT_VERSION_H
