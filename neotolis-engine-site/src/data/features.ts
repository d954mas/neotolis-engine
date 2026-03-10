export const features = [
  {
    title: 'Code-First Architecture',
    description:
      'Render loop, system order, and builder rules are all defined in game code. No heavy declarative systems.',
  },
  {
    title: 'No Hidden Magic',
    description:
      'No hidden scheduler, no hidden render graph, no automatic ECS magic. Explicit over implicit.',
  },
  {
    title: 'Composable Modules',
    description:
      'Engine is a set of features. Your game links only the modules it needs. No monolithic pipeline.',
  },
  {
    title: 'Tiny WASM Output',
    description:
      'Minimal runtime with compile-time limits and zero heap allocation in hot paths. Builds measure in kilobytes.',
  },
  {
    title: 'Data-Oriented Design',
    description:
      'Sparse component storages, dense iteration, typed handles. Predictable memory access patterns.',
  },
  {
    title: 'Builder Does the Heavy Work',
    description:
      'Offline builder validates and processes assets. Runtime only loads, resolves, and renders.',
  },
];
