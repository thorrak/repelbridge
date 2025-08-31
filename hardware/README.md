# Hardware Source Files

This directory contains the hardware source files for a RepelBridge build.


## Assembly Template

To make assembling the enclosure easier, I created a very basic [Enclosure Hole Template](./Enclosure%20Hole%20Template.pdf) which can be printed and provides a 1:1 guide for where the holes should be drilled on the face of the enclosure.

Use of this template is optional, but I found it helpful.


## JLCPCB Fabrication Files

The files needed to order a fully assembled RepelBridge from [JLCPCB](https://jlcpcb.com/) are available in the `JLCPCB Files` directory. This includes:

- Gerbers in RepelBridge.zip
- "Bill of Materials" in bom.csv with JLCPCB/LCSC part numbers
- "Pick and Place" positions file in positions.csv

These files should be ready for use to order a RepelBridge PCB from JLCPCB. Full instructions for using them are available in the [build guide](../BUILDING.md)

**NOTE about M4 Screws** - JLCMC [sells](https://jlcmc.com/product/s/E02/EDLA/FA-%E7%B4%A7%E5%9B%BA%E9%9B%B6%E4%BB%B6-%E8%9E%BA%E9%92%89?productModelNumber=EDLA-S2-M4-L6) M4x6mm screws which they will ship alongside your PCB for a very reasonable price if ordered at the same time. I **highly** recommend ordering these together with the PCB as the cost will likely be at least an order of magnitude lower than ordering separately from other sources. 


## KiCad Source Files

If you want to modify the PCB design (for example, to leverage a different enclosure) the source [KiCad](https://www.kicad.org/) files are available in the `KiCad Source` directory. 

**NOTE** - If you edit the PCB in KiCad and use the JLCPCB tools to export the BoM/PnP files, you will need to manually edit the BoM and PnP files to add the second female pin header for the microcontroller (U2) as it will not be automatically added/placed.

