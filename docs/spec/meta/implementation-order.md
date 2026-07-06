# Suggested Implementation Order

Suggested bring-up order for engine subsystems, from the platform loop and
memory policy through entities, resources, NTPACK, rendering, input, and audio,
to the builder binary and importers.

Related: [Architecture Snapshot](architecture-snapshot.md), [Core Principles](../core/principles.md)

1. platform_web + core loop + fixed/update lifecycle
2. memory arenas / alloc policy
3. entity system + hierarchy
4. sparse component storage template
5. transform + hierarchy update
6. resource ids / asset meta / pack meta
7. NTPACK format: pack parsing + asset access
8. async loading (fetch bridge + resource_step)
9. shader asset + material asset parsing
10. texture asset handling
11. mesh asset/runtime handling
12. render backend basics (WebGL 2)
13. render item build/sort
14. mesh rendering
15. sprite renderer with CPU batching
16. input polling + capture
17. audio system (web backend)
18. builder binary + importers + pack generation
19. builder audio importer
