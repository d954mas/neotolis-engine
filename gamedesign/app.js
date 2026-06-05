const STORAGE_KEY = "neotolis.gamedesign.bundle.v2";

const fallbackGdd = {
  version: 1,
  project: {
    gameId: "turkic_jam_2026",
    title: "Песнь Тамги",
    oneLiner: "Roguelite loop-builder про род кочевников, где герой идет автоматически по круговому пути, а игрок строит большой пустынный мир вокруг него.",
    theme: {
      jamTheme: "TBD",
      interpretation: "Проклятое кольцо пустыни, наследники рода и память аула."
    }
  },
  pillars: [
    {
      id: "autonomous_hero",
      title: "Автономный герой",
      body: "Игрок не управляет героем напрямую, а влияет на окружение и подготовку рода."
    },
    {
      id: "temporary_desert",
      title: "Временная пустыня",
      body: "Путь и опасности собираются вокруг кругового маршрута на текущий забег."
    },
    {
      id: "lineage_memory",
      title: "Память рода",
      body: "Смерть героя заканчивает забег, но аул и знания остаются для следующих наследников."
    }
  ],
  coreLoop: [
    "Выбрать наследника",
    "Герой автоматически идет по кольцевой дороге",
    "Герой находит ресурсы и карты",
    "Игрок ставит карты в ближние roadside и дальние field-ячейки пустыни",
    "Поставленные тайлы помогают герою или повышают риск",
    "Герой возвращается в аул, умирает или идет на следующий круг",
    "Аул получает часть результата и запускает нового наследника"
  ],
  controls: [
    {
      input: "Mouse / Tap",
      action: "Выбор карты, постановка тайла, подтверждение UI"
    }
  ],
  scope: {
    mvp: [
      "Меню, игра, game over уже связаны базовыми сценами",
      "Один круговой путь",
      "Один наследник",
      "Базовые тайлы пути, пустыни, опасности и аула",
      "Цель: пройти 10 кругов"
    ],
    stretch: [
      "Несколько типов наследников",
      "Память погибших героев",
      "Дополнительные биомы пустыни",
      "Сохранение прогресса аула"
    ],
    cutFirst: [
      "Сложная экономика",
      "Большой сюжет",
      "Много типов врагов",
      "Сложная procedural generation"
    ]
  },
  visual: {
    camera: "top_down_grid",
    gridCell: 32,
    palette: [
      {
        id: "floor",
        name: "Путь",
        color: "#d7d3c5",
        walkable: true,
        blocksVision: false
      },
      {
        id: "wall",
        name: "Граница",
        color: "#293241",
        walkable: false,
        blocksVision: true
      },
      {
        id: "hazard",
        name: "Проклятие",
        color: "#e76f51",
        walkable: true,
        blocksVision: false
      },
      {
        id: "goal",
        name: "Аул",
        color: "#2a9d8f",
        walkable: true,
        blocksVision: false
      }
    ]
  },
  entityTypes: [
    {
      id: "player_spawn",
      name: "Player Spawn",
      marker: "P",
      color: "#f4a261"
    },
    {
      id: "enemy_spawn",
      name: "Enemy Spawn",
      marker: "E",
      color: "#9d4edd"
    },
    {
      id: "pickup",
      name: "Pickup",
      marker: "+",
      color: "#3a86ff"
    }
  ]
};

function makeFallbackLevel() {
  const width = 14;
  const height = 10;
  const tiles = Array.from({ length: width * height }, (_, index) => {
    const x = index % width;
    const y = Math.floor(index / width);
    if (x === 0 || y === 0 || x === width - 1 || y === height - 1) {
      return "wall";
    }
    if ((x === 5 && y === 4) || (x === 6 && y === 4) || (x === 7 && y === 2)) {
      return "hazard";
    }
    if (x === width - 2 && y === 1) {
      return "goal";
    }
    return "floor";
  });

  return {
    version: 1,
    id: "level_01",
    name: "Первое кольцо",
    width,
    height,
    tiles,
    entities: [
      {
        id: "player_01",
        type: "player_spawn",
        x: 2,
        y: 7,
        note: "Наследник выходит из аула на первый круг."
      },
      {
        id: "enemy_01",
        type: "enemy_spawn",
        x: 10,
        y: 2,
        note: "Проверяет ранний маршрут вокруг кольца."
      },
      {
        id: "pickup_01",
        type: "pickup",
        x: 4,
        y: 5,
        note: "Награда за рискованный участок пути."
      }
    ],
    notes: "Первый уровень нужен для проверки читаемости кругового пути, аула и опасных тайлов."
  };
}

const els = {};
let state = {
  gdd: structuredClone(fallbackGdd),
  levels: [makeFallbackLevel()]
};
let selectedLevelIndex = 0;
let brushMode = "tile";
let selectedTileId = "floor";
let selectedEntityTypeId = "player_spawn";
let dirty = false;

function initElements() {
  for (const element of document.querySelectorAll("[id]")) {
    els[element.id] = element;
  }
}

async function readJson(path) {
  const response = await fetch(path, { cache: "no-store" });
  if (!response.ok) {
    throw new Error(`${path}: ${response.status}`);
  }
  return response.json();
}

async function loadInitialState() {
  const saved = localStorage.getItem(STORAGE_KEY);
  if (saved) {
    state = JSON.parse(saved);
    els.sourceStatus.textContent = "local draft";
    return;
  }

  try {
    const [gdd, level] = await Promise.all([
      readJson("data/gdd.json"),
      readJson("data/levels/level_01.json")
    ]);
    state = { gdd, levels: [level] };
    els.sourceStatus.textContent = "config files";
  } catch (error) {
    console.warn(error);
    els.sourceStatus.textContent = "fallback data";
  }
}

function saveDraft() {
  localStorage.setItem(STORAGE_KEY, JSON.stringify(state));
  dirty = true;
  updateBadges();
}

function updateBadges() {
  const level = currentLevel();
  els.dirtyBadge.textContent = dirty ? "draft" : "saved";
  els.dirtyBadge.classList.toggle("is-dirty", dirty);
  els.levelSizeBadge.textContent = `${level.width} x ${level.height}`;
}

function currentLevel() {
  return state.levels[selectedLevelIndex];
}

function tileById(id) {
  return state.gdd.visual.palette.find((tile) => tile.id === id) || state.gdd.visual.palette[0];
}

function entityTypeById(id) {
  return state.gdd.entityTypes.find((type) => type.id === id) || state.gdd.entityTypes[0];
}

function bindEvents() {
  document.querySelectorAll("[data-view]").forEach((button) => {
    button.addEventListener("click", () => setView(button.dataset.view));
  });

  document.querySelectorAll("[data-mode]").forEach((button) => {
    button.addEventListener("click", () => setBrushMode(button.dataset.mode));
  });

  bindInput(els.fieldTitle, (value) => {
    state.gdd.project.title = value;
  });
  bindInput(els.fieldGameId, (value) => {
    state.gdd.project.gameId = value;
  });
  bindInput(els.fieldOneLiner, (value) => {
    state.gdd.project.oneLiner = value;
  });
  bindInput(els.fieldJamTheme, (value) => {
    state.gdd.project.theme.jamTheme = value;
  });
  bindInput(els.fieldInterpretation, (value) => {
    state.gdd.project.theme.interpretation = value;
  });
  bindInput(els.fieldMvp, (value) => {
    state.gdd.scope.mvp = linesFromText(value);
  });
  bindInput(els.fieldStretch, (value) => {
    state.gdd.scope.stretch = linesFromText(value);
  });
  bindInput(els.fieldCutFirst, (value) => {
    state.gdd.scope.cutFirst = linesFromText(value);
  });
  bindInput(els.fieldLevelName, (value) => {
    currentLevel().name = value;
    renderLevelMeta();
  });
  bindInput(els.fieldLevelNotes, (value) => {
    currentLevel().notes = value;
  });

  els.addPillarButton.addEventListener("click", addPillar);
  els.addLoopButton.addEventListener("click", addCoreLoopStep);
  els.resizeLevelButton.addEventListener("click", resizeCurrentLevel);
  els.levelSelect.addEventListener("change", () => {
    selectedLevelIndex = Number(els.levelSelect.value);
    renderAll();
  });
  els.levelCanvas.addEventListener("mousemove", handleCanvasMove);
  els.levelCanvas.addEventListener("click", handleCanvasClick);
  els.levelCanvas.addEventListener("contextmenu", (event) => event.preventDefault());
  els.exportButton.addEventListener("click", exportBundle);
  els.copyJsonButton.addEventListener("click", copyJson);
  els.refreshJsonButton.addEventListener("click", renderJson);
  els.resetButton.addEventListener("click", resetDraft);
  els.importInput.addEventListener("change", importBundle);
}

function bindInput(input, apply) {
  input.addEventListener("input", () => {
    apply(input.value);
    saveDraft();
    renderDerived();
  });
}

function setView(view) {
  document.querySelectorAll("[data-view]").forEach((button) => {
    button.classList.toggle("is-active", button.dataset.view === view);
  });
  document.querySelectorAll(".view").forEach((section) => {
    section.classList.toggle("is-active", section.id === `${view}View`);
  });
  if (view === "data") {
    renderJson();
  }
}

function setBrushMode(mode) {
  brushMode = mode;
  document.querySelectorAll("[data-mode]").forEach((button) => {
    button.classList.toggle("is-active", button.dataset.mode === mode);
  });
}

function renderAll() {
  renderGddForm();
  renderPillars();
  renderCoreLoop();
  renderLevelSelect();
  renderLevelMeta();
  renderPalette();
  renderEntityTypes();
  renderEntities();
  drawLevel();
  renderJson();
  updateBadges();
}

function renderDerived() {
  els.projectTitle.textContent = state.gdd.project.title;
}

function renderGddForm() {
  const project = state.gdd.project;
  els.fieldTitle.value = project.title;
  els.fieldGameId.value = project.gameId;
  els.fieldOneLiner.value = project.oneLiner;
  els.fieldJamTheme.value = project.theme.jamTheme;
  els.fieldInterpretation.value = project.theme.interpretation;
  els.fieldMvp.value = textFromLines(state.gdd.scope.mvp);
  els.fieldStretch.value = textFromLines(state.gdd.scope.stretch);
  els.fieldCutFirst.value = textFromLines(state.gdd.scope.cutFirst);
  renderDerived();
}

function renderPillars() {
  els.pillarsList.replaceChildren(
    ...state.gdd.pillars.map((pillar, index) => {
      const card = document.createElement("div");
      card.className = "pillar-card";

      const title = document.createElement("input");
      title.value = pillar.title;
      title.addEventListener("input", () => {
        pillar.title = title.value;
        saveDraft();
      });

      const body = document.createElement("textarea");
      body.rows = 2;
      body.value = pillar.body;
      body.addEventListener("input", () => {
        pillar.body = body.value;
        saveDraft();
      });

      const remove = document.createElement("button");
      remove.type = "button";
      remove.textContent = "Remove";
      remove.addEventListener("click", () => {
        state.gdd.pillars.splice(index, 1);
        saveDraft();
        renderPillars();
      });

      card.append(title, body, remove);
      return card;
    })
  );
}

function renderCoreLoop() {
  els.coreLoopList.replaceChildren(
    ...state.gdd.coreLoop.map((step, index) => {
      const item = document.createElement("li");
      const row = document.createElement("div");
      row.className = "inline-edit";

      const input = document.createElement("input");
      input.value = step;
      input.addEventListener("input", () => {
        state.gdd.coreLoop[index] = input.value;
        saveDraft();
      });

      const remove = document.createElement("button");
      remove.type = "button";
      remove.textContent = "Remove";
      remove.addEventListener("click", () => {
        state.gdd.coreLoop.splice(index, 1);
        saveDraft();
        renderCoreLoop();
      });

      row.append(input, remove);
      item.append(row);
      return item;
    })
  );
}

function renderLevelSelect() {
  els.levelSelect.replaceChildren(
    ...state.levels.map((level, index) => {
      const option = document.createElement("option");
      option.value = String(index);
      option.textContent = `${level.id}: ${level.name}`;
      option.selected = index === selectedLevelIndex;
      return option;
    })
  );
}

function renderLevelMeta() {
  const level = currentLevel();
  els.fieldLevelName.value = level.name;
  els.fieldLevelWidth.value = level.width;
  els.fieldLevelHeight.value = level.height;
  els.fieldLevelNotes.value = level.notes || "";
  els.levelNameHeading.textContent = level.name;
  renderLevelSelect();
  updateBadges();
}

function renderPalette() {
  els.paletteList.replaceChildren(
    ...state.gdd.visual.palette.map((tile) => {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "palette-item";
      button.classList.toggle("is-active", selectedTileId === tile.id);
      button.addEventListener("click", () => {
        selectedTileId = tile.id;
        setBrushMode("tile");
        renderPalette();
      });
      button.append(makeSwatch(tile.color), document.createTextNode(tile.name));
      return button;
    })
  );
}

function renderEntityTypes() {
  els.entityTypeList.replaceChildren(
    ...state.gdd.entityTypes.map((type) => {
      const button = document.createElement("button");
      button.type = "button";
      button.className = "palette-item";
      button.classList.toggle("is-active", selectedEntityTypeId === type.id);
      button.addEventListener("click", () => {
        selectedEntityTypeId = type.id;
        setBrushMode("entity");
        renderEntityTypes();
      });
      button.append(makeMarker(type), document.createTextNode(type.name));
      return button;
    })
  );
}

function renderEntities() {
  const level = currentLevel();
  els.entityList.replaceChildren(
    ...level.entities.map((entity, index) => {
      const type = entityTypeById(entity.type);
      const row = document.createElement("div");
      row.className = "entity-row";

      const text = document.createElement("div");
      text.innerHTML = `<strong>${entity.id}</strong><br><span class="muted">${entity.x}, ${entity.y}</span>`;

      const remove = document.createElement("button");
      remove.type = "button";
      remove.textContent = "Remove";
      remove.addEventListener("click", () => {
        level.entities.splice(index, 1);
        saveDraft();
        renderEntities();
        drawLevel();
      });

      row.append(makeMarker(type), text, remove);
      return row;
    })
  );
}

function makeSwatch(color) {
  const swatch = document.createElement("span");
  swatch.className = "swatch";
  swatch.style.background = color;
  return swatch;
}

function makeMarker(type) {
  const marker = document.createElement("span");
  marker.className = "marker";
  marker.style.background = type.color;
  marker.textContent = type.marker;
  return marker;
}

function drawLevel() {
  const level = currentLevel();
  const cell = Number(state.gdd.visual.gridCell) || 32;
  const canvas = els.levelCanvas;
  const context = canvas.getContext("2d");
  canvas.width = level.width * cell;
  canvas.height = level.height * cell;

  context.clearRect(0, 0, canvas.width, canvas.height);
  for (let y = 0; y < level.height; y++) {
    for (let x = 0; x < level.width; x++) {
      const tile = tileById(level.tiles[indexOf(level, x, y)]);
      context.fillStyle = tile.color;
      context.fillRect(x * cell, y * cell, cell, cell);
      context.strokeStyle = "rgba(32, 36, 44, 0.24)";
      context.strokeRect(x * cell + 0.5, y * cell + 0.5, cell, cell);
    }
  }

  context.textAlign = "center";
  context.textBaseline = "middle";
  context.font = "700 17px system-ui";
  for (const entity of level.entities) {
    const type = entityTypeById(entity.type);
    const cx = entity.x * cell + cell / 2;
    const cy = entity.y * cell + cell / 2;
    context.fillStyle = type.color;
    context.beginPath();
    context.roundRect(cx - 12, cy - 12, 24, 24, 5);
    context.fill();
    context.fillStyle = "#fff";
    context.fillText(type.marker, cx, cy + 1);
  }
}

function handleCanvasMove(event) {
  const cell = Number(state.gdd.visual.gridCell) || 32;
  const point = canvasPoint(event);
  const x = Math.floor(point.x / cell);
  const y = Math.floor(point.y / cell);
  els.cellStatus.textContent = inBounds(currentLevel(), x, y) ? `x: ${x}, y: ${y}` : "x: -, y: -";
}

function handleCanvasClick(event) {
  const level = currentLevel();
  const cell = Number(state.gdd.visual.gridCell) || 32;
  const point = canvasPoint(event);
  const x = Math.floor(point.x / cell);
  const y = Math.floor(point.y / cell);
  if (!inBounds(level, x, y)) {
    return;
  }

  if (brushMode === "tile") {
    level.tiles[indexOf(level, x, y)] = selectedTileId;
  } else if (brushMode === "entity") {
    const existing = level.entities.find((entity) => entity.x === x && entity.y === y);
    if (existing) {
      existing.type = selectedEntityTypeId;
    } else {
      level.entities.push({
        id: nextEntityId(level, selectedEntityTypeId),
        type: selectedEntityTypeId,
        x,
        y,
        note: ""
      });
    }
  } else {
    level.entities = level.entities.filter((entity) => entity.x !== x || entity.y !== y);
    level.tiles[indexOf(level, x, y)] = "floor";
  }

  saveDraft();
  renderEntities();
  drawLevel();
}

function canvasPoint(event) {
  const rect = els.levelCanvas.getBoundingClientRect();
  return {
    x: ((event.clientX - rect.left) / rect.width) * els.levelCanvas.width,
    y: ((event.clientY - rect.top) / rect.height) * els.levelCanvas.height
  };
}

function resizeCurrentLevel() {
  const level = currentLevel();
  const nextWidth = clampInt(els.fieldLevelWidth.value, 4, 64);
  const nextHeight = clampInt(els.fieldLevelHeight.value, 4, 64);
  const oldTiles = level.tiles;
  const oldWidth = level.width;
  const nextTiles = Array.from({ length: nextWidth * nextHeight }, (_, index) => {
    const x = index % nextWidth;
    const y = Math.floor(index / nextWidth);
    if (x < oldWidth && y < level.height) {
      return oldTiles[y * oldWidth + x] || "floor";
    }
    return "floor";
  });

  level.width = nextWidth;
  level.height = nextHeight;
  level.tiles = nextTiles;
  level.entities = level.entities.filter((entity) => inBounds(level, entity.x, entity.y));
  saveDraft();
  renderLevelMeta();
  renderEntities();
  drawLevel();
}

function addPillar() {
  const count = state.gdd.pillars.length + 1;
  state.gdd.pillars.push({
    id: `pillar_${count}`,
    title: `Опора ${count}`,
    body: "Новая дизайн-опора."
  });
  saveDraft();
  renderPillars();
}

function addCoreLoopStep() {
  state.gdd.coreLoop.push("Новый шаг");
  saveDraft();
  renderCoreLoop();
}

function renderJson() {
  els.jsonOutput.value = JSON.stringify(state, null, 2);
}

function exportBundle() {
  const blob = new Blob([JSON.stringify(state, null, 2)], {
    type: "application/json"
  });
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = "neotolis-gamedesign-bundle.json";
  link.click();
  URL.revokeObjectURL(url);
  dirty = false;
  updateBadges();
}

async function copyJson() {
  renderJson();
  await navigator.clipboard.writeText(els.jsonOutput.value);
}

function resetDraft() {
  localStorage.removeItem(STORAGE_KEY);
  state = {
    gdd: structuredClone(fallbackGdd),
    levels: [makeFallbackLevel()]
  };
  selectedLevelIndex = 0;
  dirty = false;
  els.sourceStatus.textContent = "reset fallback";
  renderAll();
}

async function importBundle(event) {
  const [file] = event.target.files;
  if (!file) {
    return;
  }
  const text = await file.text();
  const parsed = JSON.parse(text);
  if (!parsed.gdd || !Array.isArray(parsed.levels)) {
    throw new Error("Bundle must contain gdd and levels.");
  }
  state = parsed;
  selectedLevelIndex = 0;
  saveDraft();
  renderAll();
  event.target.value = "";
}

function indexOf(level, x, y) {
  return y * level.width + x;
}

function inBounds(level, x, y) {
  return x >= 0 && y >= 0 && x < level.width && y < level.height;
}

function nextEntityId(level, type) {
  let index = 1;
  let id = `${type}_${String(index).padStart(2, "0")}`;
  while (level.entities.some((entity) => entity.id === id)) {
    index++;
    id = `${type}_${String(index).padStart(2, "0")}`;
  }
  return id;
}

function linesFromText(text) {
  return text
    .split("\n")
    .map((line) => line.trim())
    .filter(Boolean);
}

function textFromLines(lines) {
  return Array.isArray(lines) ? lines.join("\n") : "";
}

function clampInt(value, min, max) {
  const number = Number.parseInt(value, 10);
  if (Number.isNaN(number)) {
    return min;
  }
  return Math.max(min, Math.min(max, number));
}

initElements();
loadInitialState()
  .then(() => {
    bindEvents();
    renderAll();
  })
  .catch((error) => {
    console.error(error);
    els.sourceStatus.textContent = "load error";
  });
