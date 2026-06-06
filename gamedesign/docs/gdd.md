# Песнь Тамги - GDD

Версия: 0.3  
Статус: рабочий GDD для джема  
Роль: источник решений для геймдизайна, арта и реализации Claude.

## Как читать

Документы идут от общего к частному:

1. [00_concept.md](00_concept.md) - крупный концепт и фантазия игры.
2. [01_world_layout.md](01_world_layout.md) - как устроены аул, дорога и пустынные ячейки.
3. [02_core_loop.md](02_core_loop.md) - основной цикл игрока и героя.
4. [03_systems.md](03_systems.md) - смерть, Тамга, реликт, аул, карты.
5. [04_content_balance.md](04_content_balance.md) - контент MVP и связь с `.ini` / `.tsv` балансом.
6. [05_art_direction.md](05_art_direction.md) - визуальный стиль, палитра, UI и иконки.
7. [06_claude_brief.md](06_claude_brief.md) - что Claude должен реализовывать и что нельзя потерять.
8. [07_ftue.md](07_ftue.md) - первый опыт игрока и интро.
9. [08_loop_hero_research_review.md](08_loop_hero_research_review.md) - исследование Loop Hero, ревью GDD и план следующих шагов.
10. [09_player_actions.md](09_player_actions.md) - действия игрока, карты, смерть и loop между забегами.
11. [10_loop_hero_cards_reference.md](10_loop_hero_cards_reference.md) - как устроены карты и синергии Loop Hero, что переносим в нашу систему.
12. [11_ftue_first_loop_plan.md](11_ftue_first_loop_plan.md) - FTUE, первый круг, смерть, новый герой и acceptance checklist.
13. [12_playable_developer_plan.md](12_playable_developer_plan.md) - план для разработчика после ревью Code: build-slots, карты, смерть, аул, новый герой.
14. [13_event_log_and_texts.md](13_event_log_and_texts.md) - журнал событий, точки решения, первые шаблоны текста.
15. [14_gdd_art_work_plan.md](14_gdd_art_work_plan.md) - план GDD/арт-работы на время реализации playable.
16. [15_first_cards_content.md](15_first_cards_content.md) - первый контент карт, роли, тексты, следующие кандидаты.
17. [16_ui_ux_layout.md](16_ui_ux_layout.md) - 16:9 gameplay UI, HUD, рука карт, лог, состояния экрана.
18. [17_first_10_minutes.md](17_first_10_minutes.md) - поминутный сценарий первых 10 минут playable.
19. [18_playable_review_checklist.md](18_playable_review_checklist.md) - чеклист ревью первого playable-билда.
20. [20_tile_placement_system.md](20_tile_placement_system.md) - зоны карты, placement и правила тайлов.
21. [21_ftue_best_practices_research.md](21_ftue_best_practices_research.md) - исследование хороших практик FTUE и ревью текущего сценария.
22. [22_ftue_production_script.md](22_ftue_production_script.md) - подробный production-script FTUE по состояниям для реализации.
23. [23_ftue_analysis_and_improvement_plan.md](23_ftue_analysis_and_improvement_plan.md) - анализ перегруза FTUE и план улучшений по фазам.
24. [24_narrative_designer_role.md](24_narrative_designer_role.md) - роль нарративного дизайнера и правила работы с текстом.
25. [25_narrative_bible.md](25_narrative_bible.md) - базовая нарративная библия: темы, тон, словарь, guardrails.
26. [26_ftue_step_1_narrative.md](26_ftue_step_1_narrative.md) - нарративный baseline первых 6 секунд и первого клика FTUE.
27. [27_cultural_research_backlog.md](27_cultural_research_backlog.md) - backlog культурного ресерча и красные зоны до источников.
28. [28_current_source_of_truth.md](28_current_source_of_truth.md) - актуальный короткий контракт после ревью документов и чата с Code.
29. [29_card_tile_families_review.md](29_card_tile_families_review.md) - ревью карт-тайлов, семейств и player-facing решения для `wolf_track`.
30. [30_narrative_content_baseline.md](30_narrative_content_baseline.md) - нарративный baseline FTUE, карт, событий, синергий и первых вещей.
31. [31_visual_production_master_plan.md](31_visual_production_master_plan.md) - production-план всего визуала: UI kit, карты, тайлы, персонажи, экипировка, иконки, FX и интеграция в игру.
32. [32_asset_production_batch_a.md](32_asset_production_batch_a.md) - конкретный Batch A для production PNG: UI 9-slice, ground/decor/road/buffer, аул, первые тайлы, герой, размеры, alpha, slice9, crop/acceptance.
33. [33_asset_production_batch_b.md](33_asset_production_batch_b.md) - Batch B для production PNG: playable cards, HUD icons, equipment/hero panel, first FX, размеры, alpha, acceptance и Code request.
34. [34_asset_production_batch_c.md](34_asset_production_batch_c.md) - Batch C для future visual library: будущие тайлы, этапы аула, FTUE/death memory FX, архетипы героя и future icons.
35. [35_runtime_visual_qa_checklist.md](35_runtime_visual_qa_checklist.md) - runtime QA checklist: raw PNG -> builder -> atlas bind -> drawn state -> screenshot proof.
36. [36_visual_asset_status_matrix.md](36_visual_asset_status_matrix.md) - current visual asset matrix: raw inventory, batch status, runtime status, final-art gaps and next acceptance targets.
37. [37_final_art_repaint_pass_1.md](37_final_art_repaint_pass_1.md) - first final repaint contract for visible gameplay UI, cards, HUD icons, hero, saxaul, aul and road/buffer.
38. [38_final_repaint_pass_1_delivery_review.md](38_final_repaint_pass_1_delivery_review.md) - delivered repaint pass 1 files, technical verification, art lead review and next screenshot QA gates.
39. [39_final_repaint_pass_2_contract.md](39_final_repaint_pass_2_contract.md) - second final repaint contract for hero movement, hero panel, equipment, HUD stat icons and card utility icons.
40. [40_final_repaint_pass_2_delivery_review.md](40_final_repaint_pass_2_delivery_review.md) - delivered repaint pass 2 files, technical verification, art lead review and next runtime screenshot QA gates.
41. [41_final_repaint_pass_3_world_map_contract.md](41_final_repaint_pass_3_world_map_contract.md) - third final repaint contract for ground, decor, road, buffer, aul and remaining active world tiles.
42. [42_visual_completion_board.md](42_visual_completion_board.md) - active board for all visual passes, candidate-final status, runtime QA gaps and next owners.
43. [43_final_repaint_pass_3_world_map_delivery_review.md](43_final_repaint_pass_3_world_map_delivery_review.md) - delivered world/map repaint files, technical verification, art lead review and next runtime screenshot QA gates.
44. [44_final_repaint_pass_4_fx_contract.md](44_final_repaint_pass_4_fx_contract.md) - fourth final repaint contract for first playable feedback FX plus FTUE/death/memory future FX.
45. [45_final_repaint_pass_5_aul_upgrades_contract.md](45_final_repaint_pass_5_aul_upgrades_contract.md) - fifth final repaint contract for aul upgrade stages and Tamga post.
46. [46_final_repaint_pass_4_fx_delivery_review.md](46_final_repaint_pass_4_fx_delivery_review.md) - delivered FX repaint files, technical verification, art lead review and next runtime FX QA gates.
47. [47_final_repaint_pass_5_aul_upgrades_delivery_review.md](47_final_repaint_pass_5_aul_upgrades_delivery_review.md) - delivered aul upgrade repaint files, technical verification, art lead review and next registry/runtime QA gates.
48. [48_runtime_visual_qa_harness_spec.md](48_runtime_visual_qa_harness_spec.md) - QA-only visual harness request for proving L4/L5 screenshots across gameplay, UI, cards, equipment, FX and aul progression.
49. [49_final_repaint_pass_6_future_tile_card_library_contract.md](49_final_repaint_pass_6_future_tile_card_library_contract.md) - sixth final repaint contract for remaining future tile/card/icon placeholder PNGs.
50. [50_final_repaint_pass_6_future_tile_card_library_delivery_review.md](50_final_repaint_pass_6_future_tile_card_library_delivery_review.md) - delivered future tile/card/icon repaint files, technical verification, art lead review and next registry/runtime QA gates.
51. [51_final_repaint_pass_7_hero_archetype_panels_contract.md](51_final_repaint_pass_7_hero_archetype_panels_contract.md) - seventh final repaint contract for hero archetype panel dolls and small icon readability fixes.
52. [52_runtime_visual_qa_devapi_evidence.md](52_runtime_visual_qa_devapi_evidence.md) - desktop/native devapi evidence for visual QA reachability and nonzero atlas bind counts.
53. [53_final_repaint_pass_7_hero_archetype_panels_delivery_review.md](53_final_repaint_pass_7_hero_archetype_panels_delivery_review.md) - delivered hero archetype panel repaint files, technical verification, art review and next registry/runtime QA gates.
54. [54_art_source_policy_generated_bitmap.md](54_art_source_policy_generated_bitmap.md) - production art source policy: candidate-final art should come from generated bitmap / painted source, not pure SVG-like script placeholders.
55. [55_desktop_l5_visual_capture_contract.md](55_desktop_l5_visual_capture_contract.md) - desktop/native L5 capture/readback contract for proving real runtime pixel readability.
56. [56_generated_bitmap_art_audit.md](56_generated_bitmap_art_audit.md) - audit separating pipeline/script technical art from generated bitmap production art and defining the next targeted repaint gate.
57. [57_pass_8_generated_bitmap_repaint_readiness.md](57_pass_8_generated_bitmap_repaint_readiness.md) - prepared Pass 8 priority list for targeted generated bitmap repaint after L5 screenshot review.
58. [58_pass_8_generated_card_art_delivery_review.md](58_pass_8_generated_card_art_delivery_review.md) - generated bitmap card art delivery, pack/L5 evidence, and card layout fix request before final acceptance.
59. [59_pass_9_generated_active_tiles_delivery_review.md](59_pass_9_generated_active_tiles_delivery_review.md) - generated bitmap active tile delivery, rejected magenta-source note, pack/L5 evidence, and post-map-migration review gate.
60. [60_pass_10_generated_wayfarer_equipment_contract.md](60_pass_10_generated_wayfarer_equipment_contract.md) - generated bitmap contract for wayfarer map sprites, hero panel doll and equipment kit.
61. [61_pass_10_generated_wayfarer_equipment_delivery_review.md](61_pass_10_generated_wayfarer_equipment_delivery_review.md) - delivered generated wayfarer/equipment runtime files, pack/L5 evidence and final review risks.
62. [62_pass_11_generated_hud_icons_contract.md](62_pass_11_generated_hud_icons_contract.md) - generated bitmap contract for HUD, stat, utility and future icons.
63. [63_pass_11_generated_hud_icons_delivery_review.md](63_pass_11_generated_hud_icons_delivery_review.md) - delivered generated HUD/icon runtime files, pack/L5 evidence and final review risks.
64. [64_pass_12_generated_ui_surfaces_contract.md](64_pass_12_generated_ui_surfaces_contract.md) - generated bitmap contract for UI panels, cards, chips, slots and tooltip/button surfaces.
65. [65_pass_12_generated_ui_surfaces_delivery_review.md](65_pass_12_generated_ui_surfaces_delivery_review.md) - delivered generated UI surface runtime files, pack/L5 evidence and final review risks.
66. [66_visual_design_bible.md](66_visual_design_bible.md) - current visual design bible for generated UI materials, buttons, cards, panels, icons and runtime acceptance.
67. [67_art_generation_and_reuse_protocol.md](67_art_generation_and_reuse_protocol.md) - mandatory art workflow protocol: generated bitmap source, component inventory, reuse map, no SVG-looking production art.
68. [68_pass_13_world_foundation_contract.md](68_pass_13_world_foundation_contract.md) - source-first generated bitmap contract for world foundation: ground, decor, road, road buffer and starting aul.
69. [69_pass_13_world_foundation_source_review.md](69_pass_13_world_foundation_source_review.md) - GDD source-review gate for Pass 13 before slicing, runtime overwrite or Code work.
70. [70_gameplay_map_zoom_visual_review.md](70_gameplay_map_zoom_visual_review.md) - GDD review of the larger desktop gameplay map scale and visual implications for Pass 13.
71. [71_pass_13_world_foundation_source_review.md](71_pass_13_world_foundation_source_review.md) - GDD review of the generated Pass 13 world source sheet, accepted with restrictions before slicing.
72. [72_visual_design_bible_enforcement.md](72_visual_design_bible_enforcement.md) - mandatory GDD enforcement gate: no SVG-looking production art, no repeated one-off UI generation, source sheet plus reuse map required.
73. [73_pass_13_slicing_proposal_review.md](73_pass_13_slicing_proposal_review.md) - GDD review rejecting Pass 13 slicing proposal for runtime overwrite and requesting cleaner V2 crops from the same source.
74. [74_runtime_ui_button_cleanup.md](74_runtime_ui_button_cleanup.md) - runtime cleanup removing old demo one-off button art and keeping UI buttons on the generated Pass 12 material system.
75. [75_pass_13_slicing_proposal_v2_review.md](75_pass_13_slicing_proposal_v2_review.md) - GDD review partially accepting Pass 13 V2 ground/decor/aul candidates and rejecting road/buffer for V3.
76. [76_pass_13_partial_runtime_integration_review.md](76_pass_13_partial_runtime_integration_review.md) - runtime integration review for accepted Pass 13 V2 ground/decor/aul subset, with screenshot evidence and next Code/Art tasks.
77. [77_pass_13_road_buffer_v3_review.md](77_pass_13_road_buffer_v3_review.md) - GDD review accepting V3 road/buffer source direction but rejecting runtime slices for alignment and cleanup.
78. [78_build_slot_overlay_runtime_review.md](78_build_slot_overlay_runtime_review.md) - GDD runtime review accepting softened build-slot overlay so generated map art remains readable.
79. [79_visual_source_registry_and_reuse_queue.md](79_visual_source_registry_and_reuse_queue.md) - active source registry and reuse queue preventing SVG-looking art and repeated one-off UI generation.
80. [80_pass_13_road_buffer_v4_review.md](80_pass_13_road_buffer_v4_review.md) - GDD review accepting revised V4 road/buffer candidates for runtime copy.
81. [81_cyrillic_runtime_text_review.md](81_cyrillic_runtime_text_review.md) - runtime text review accepting current Cyrillic UI fix.
82. [82_pass_13_road_buffer_runtime_review.md](82_pass_13_road_buffer_runtime_review.md) - runtime screenshot review accepting integrated Pass 13 V4 road/buffer for the current playable.
83. [83_normal_gameplay_visual_review_request.md](83_normal_gameplay_visual_review_request.md) - next visual QA request for a dense gameplay screenshot covering cards, active tiles, HUD/icons and hero/equipment.
84. [84_dense_visual_qa_screenshot_review.md](84_dense_visual_qa_screenshot_review.md) - GDD review of the dense visual QA screenshot, accepting evidence and defining targeted fixes only.
85. [85_dense_visual_qa_followup_review.md](85_dense_visual_qa_followup_review.md) - GDD review of empty-card treatment and map-context active tiles, confirming Beast Trail targeted art fix.
86. [86_pass_14_beast_trail_targeted_art_contract.md](86_pass_14_beast_trail_targeted_art_contract.md) - targeted generated-bitmap contract for `Звериная тропа` tile/card art only.
87. [87_pass_14_beast_trail_delivery_review.md](87_pass_14_beast_trail_delivery_review.md) - Pass 14 source accepted, first slices rejected, revision requested.
88. [88_ui_asset_kit_contract.md](88_ui_asset_kit_contract.md) - mandatory generated-bitmap reusable UI kit contract.
89. [89_pass_15_ui_asset_kit_work_order.md](89_pass_15_ui_asset_kit_work_order.md) - Pass 15 UI inventory/contact work order.
90. [90_pass_14_beast_trail_revision_approval.md](90_pass_14_beast_trail_revision_approval.md) - revised Beast Trail slices approved for Code integration.
91. [91_pass_14_beast_trail_runtime_review.md](91_pass_14_beast_trail_runtime_review.md) - Beast Trail runtime integration accepted for current playable.
92. [92_pass_15_ui_inventory_review.md](92_pass_15_ui_inventory_review.md) - Pass 15 UI inventory/reuse map accepted.
93. [93_pass_15_ui_contact_review.md](93_pass_15_ui_contact_review.md) - Pass 15 contact accepted as reuse proof, not final UI acceptance.
94. [94_pass_15_runtime_ui_reuse_review.md](94_pass_15_runtime_ui_reuse_review.md) - Pass 15 runtime UI reuse proof accepted; normal gameplay placement-state proof requested.
96. [96_ui_design_bible_component_families.md](96_ui_design_bible_component_families.md) - UI design bible for generated-bitmap reusable component families.

Исходные материалы сохранены отдельно:

- [song_of_tamga_concept.md](song_of_tamga_concept.md)
- [song_of_tamga_final_ideas.md](song_of_tamga_final_ideas.md)
- [song_of_tamga_gdd.md](song_of_tamga_gdd.md)

## Одно предложение

**Песнь Тамги** - тюркско-кочевая игра-сказка и roguelite loop-builder в духе Loop Hero: аул стоит в центре, герой сам идет по кольцевой дороге вокруг него, а игрок занимает ближние и дальние ячейки пустыни тайлами, чтобы провести одного наследника через 10 кругов.

## Главные решения

| Вопрос | Решение |
| --- | --- |
| Референс | Loop Hero, но вместо некромантии и темного фэнтези - кочевой род, аул, тамги, память погибших. |
| Управление | Герой движется автоматически. Игрок строит окружение дороги и принимает решения между кругами. |
| Карта | Аул в центре, дорога-кольцо, no-build road buffer и большой скроллящийся field вокруг. |
| Цель | Один наследник должен пройти 10 кругов подряд. |
| Смерть | Не game over. Герой оставляет Последнюю Тамгу, будущий герой может ее забрать. |
| Прогресс | Стойбище/аул и знания рода постоянные. Пустыня текущего забега временная. |
| Мета-дуга | Стойбище рода растет до аула, укрепленного поселения, степного города, восточной столицы и ханства/султаната/каганата. |
| Баланс | Числа живут в `games/turkic-jam-2026/config/*.ini/*.tsv`. GDD описывает смысл и правила. |

Актуальные решения после ревью собраны в [28_current_source_of_truth.md](28_current_source_of_truth.md). Если старые заметки спорят с ним, считать старую заметку устаревшей.

## Что важно не потерять

- Аул по центру.
- Аул начинается как маленькое стойбище рода, а не как готовая столица.
- Дорога вокруг аула.
- Большая пустыня со скроллом: ближние `roadside` и дальние `field` ячейки, которые занимает игрок.
- Герой не управляется напрямую.
- Смерть оставляет цель для следующего героя.
- Пустыня после смерти не сохраняется целиком.
- Тон: тюркско-кочевая память рода, не арабская сказка.

## Прокачка аула как этапы

Каждый этап аула должен менять и визуал центра карты, и правила игры:

```text
стойбище рода
-> родовой аул
-> укрепленный аул
-> торговое поселение
-> степной город
-> восточная столица
-> ханство / султанат / каганат
```

Для MVP нужен первый unlock: смерть сохраняет часть ресурсов в стойбище, новый наследник выходит уже из усиленного места.
