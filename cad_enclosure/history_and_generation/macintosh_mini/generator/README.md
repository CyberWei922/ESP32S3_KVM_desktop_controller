# Macintosh 128K 风格迷你桌面控制器

这是独立于 `enclosure/` 斜屏版本的新设计。外形参考1984年 Macintosh 128K 的竖直一体机比例、圆角米色机身、内凹横屏、软驱槽、铭牌位、顶部提手凹槽和后部散热孔；没有覆盖或修改上一版文件。

## 打印文件

- `cad_enclosure/latest_output/macintosh_mini/01_macintosh_body.stl`：一体式圆角主机身，后部敞开
- `cad_enclosure/latest_output/macintosh_mini/02_inset_back_cover.stl`：内嵌式可拆后盖
- `cad_enclosure/latest_output/macintosh_mini/03_internal_lcd_clamp.stl`：隐藏在机内的LCD压框

所有STL单位均为毫米，并已验证为封闭流形实体。

## 外形与空间

- 机身主体：100 W × 84 D × 140 H mm
- 含底脚总高度：约141.5 mm
- 壁厚：2.8 mm
- 正面厚度：4.0 mm
- 内部最大空间约：94.4 W × 77.2 D × 134.4 H mm
- 后盖：93.9 W × 133.9 H × 2.4 T mm
- 底部四角带直径14 mm宽脚，每个脚底预留直径11.2 mm、深1.2 mm的硅胶防滑垫凹槽

内部空间可以容纳：

- ESP32-S3双USB-C开发板，约57.2 × 28 × 15 mm
- 50 × 70 mm洞洞板，可竖直固定在后盖内侧
- 单路继电器模块
- LCD排线、按钮线和散热器控制线

## 正面结构

- LCD按横屏安装，使窗口比例更接近初代Macintosh
- 可视窗口：50.0 × 37.8 mm
- 屏幕外部凹槽：64 × 50 mm
- 三个MX轴开口：14.2 × 14.2 mm
- 轴体中心间距：19.05 mm
- 屏幕正面没有螺丝孔
- 软驱槽和左侧铭牌位为装饰性浅凹槽

三个轴体开口目前按照标准MX定位板尺寸设计。已经购买的三轴固定器如果不是19.05 mm中心距，需要按实物修改脚本中的 `MX_HOLE` 和 `MX_PITCH`。

## 装配方法

### LCD

1. 将2.4寸LCD旋转90°，从后部放到屏幕窗口内侧。
2. 将内部LCD压框盖在PCB背面。
3. 使用4颗M2.5 × 8 mm自攻螺丝，从机内锁入正面隐藏柱。

当前压框按常见约60.5 × 42.5 mm的ILI9341模块估算。打印前必须实测LCD PCB长宽、厚度和排针位置。

### 后盖

- 使用4颗M3 × 8～10 mm沉头自攻螺丝
- 后盖螺丝头为沉入式
- 主体螺丝柱通过侧向加强筋连接外壁
- 后盖带7条散热槽、两个USB-C开孔和一个9 mm通用出线孔

### ESP32和洞洞板

推荐将50 × 70 mm洞洞板竖直固定在后盖内侧，再把ESP32通过2.54 mm母排针插在洞洞板上。确认USB-C位置后，可移动脚本中的后盖接口孔，保证插头能直线插入。

为了避免按机械轴时机身在光滑桌面上后移，建议安装4个直径10～11 mm的自粘硅胶垫，并在机身内部最低处使用VHB胶固定一块约70 × 50 × 2～3 mm的小钢片作为配重。配重应与电路板之间保留绝缘层。

## 建议打印参数

- 材料：PLA或PETG
- 喷嘴：0.4 mm
- 层高：0.20 mm
- 壁线：3～4圈
- 填充：15%～25%
- 主体：底部朝下打印，屏幕凹槽顶部可能需要局部支撑
- 后盖和LCD压框：平放打印

## 重新生成

```bash
.cadenv/bin/python 'cad_enclosure/history_and_generation/macintosh_mini/generator/generate_macintosh_mini.py'
```

关键尺寸集中在生成脚本顶部，修改后会覆盖 `cad_enclosure/latest_output/macintosh_mini/` 中的当前输出，不影响另一个外壳方案。
