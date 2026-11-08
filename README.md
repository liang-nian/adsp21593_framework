# ADSP-21593 Audio Framework

Dual-SHARC audio firmware. Hardware, chips, and drivers are described in [doc/adsp21593_framework.md](doc/adsp21593_framework.md).

## Environment

- CrossCore Embedded Studio 3.0.3
- Python 3
- ICE-1000, for download and debug
- Core board EV-21593-SOM and carrier EV-SOMCRR-EZKIT. For A2B, also the AD2428MINI expansion board and USBi

## Build

Open `sharc1` and `sharc2` in CCES and build both projects.

## Loader image

After both cores are built, run this from the repo root:

```powershell
python post_build.py
```

The script takes the dxe and map.xml from each core's Debug directory, or Release when Debug is absent. Output goes to `output/`. The dual-core SPI image is `output/adsp21593.ldr`.

`elfloader.exe` comes from CCES. The script uses `CCES_HOME` when it is set, otherwise `E:\bin\cces\3.0.3`.

## Host

The virtualenv lives at the repo root. From the root:

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python -m pip install -r host\requirements.txt
.\.venv\Scripts\python host\app.py
```

The baud box starts at `921600`. Choose the serial port, then click **打开**.

[MIT License](LICENSE).
