# 3D 打印外壳

本目录按“当前输出”和“历史版本与生成代码”分开管理。

```text
cad_enclosure/
├── latest_output/
│   ├── apple_retro_hybrid/
│   └── macintosh_mini/
└── history_and_generation/
    ├── apple_retro_hybrid/
    │   ├── releases/
    │   └── generator/
    └── macintosh_mini/
        ├── releases/
        └── generator/
```

`latest_output` 只保留当前最新版 STL。历史版本使用 ZIP 保存；生成脚本放在对应方案的 `generator` 目录中。回档时，将指定 ZIP 中的 STL 恢复到 `latest_output` 对应目录。

当前 `apple_retro_hybrid` 为 2026-09-11 的 V119 Macintosh 重构版：恢复上舱底板走线孔，并与底座顶板开口统一为36 × 22 mm、圆角3 mm、中心X=0/Y=22 mm，上下投影完全重合。V117的定位槽、定位环、5条后盖散热孔和后盖四边法向0.25 mm打印余量全部保留。V117快照已归档；详细尺寸、连接方式和打印参数见`history_and_generation/apple_retro_hybrid/generator/README.md`。
