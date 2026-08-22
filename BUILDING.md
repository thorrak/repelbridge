# Building a RepelBridge

## Introduction / About this Guide

This guide covers the steps that I used when building the RepelBridges that I have managing the Liv controllers in my home. Although most of the individual components I recommend can be replaced with similar alternatives, these replacements will almost certainly require changes elsewhere to the build - especially to the PCB design. I will try to provide some guidance as to why I selected certain components, but the expectation is that someone following this guide will purchase the components specified. If you make substitutions, you will be responsible for identifying other potential changes required on your own.


### Estimated Build Cost & Time

I estimate that my controllers - excluding tax, shipping, and tariffs - cost **$97.14** each to build - but it warrants mentioning that the minimum order quantity from JLCPCB for the PCB is 2. Each controller took about **20 minutes to assemble**, including drilling, wiring, and flashing. 

## Ordering the Parts

Most of the parts used in this build are dictated by the enclosure used and the PCB.

### Enclosure

The enclosure used for this build is [this one](https://www.amazon.com/dp/B0D97GQRGX) (the LeMotech IP67 6.9" x 4.9" x 3" Waterproof Junction Box). This junction is the perfect size to fit all of the required electronics, and is both dust and waterproof, meaning it should be safe in the elements. It has two 4mm heat set inserts already attached inside the case, providing an easy place to secure the PCB.

Although there are many similar junction boxes, the PCB was designed to fit this box's specific dimensions, including exact placement for the corner notches & screw holes. If you choose a different enclosure, you will need to adjust the PCB accordingly. 

![The PCB was specifically designed for this enclosure](/img/lemotech_dimensions.png "Enclosure Dimensions")


### PCB

The heart of a RepelBridge is the PCB. The PCB can be ordered _assembled_ from JLCPCB - which I **highly** recommend over attempting to assemble it yourself. All files necessary to order the PCB are available in the `hardware/JLCPCB Files` directory of this repo, including the files necessary for assembly.

#### Ordering the PCB

To order the PCB from JLCPCB, visit [https://www.jlcpcb.com/](https://www.jlcpcb.com/) and do the following:

1. Click **"Add gerber file"** and upload the gerbers (`RepelBridge.zip`)
2. On the following screen, after the PCB design loads, change **"Mark on PCB"** to **"Order Number(Specify Position)"**. Feel free to change the PCB color/other options if you choose.
3. Click the "switch" next to **"PCB Assembly"**.
4. Change **"Tooling holes"** to **"Added by Customer"**. Optionally change the PCBA Qty if you do not need 5 assembled PCBs.
5. Click **"Next"** to proceed to the gerber viewer. Click **"Next"** again to proceed to the "BoM" and "CPL" upload screen.
6. Upload the Bill of Materials CSV file (`bom.csv`) by clicking the **"Add BOM File"** button
7. Upload the Pick-and-Place Location CSV file (`positions.csv`) by clicking the **"Add CPL File"** button
8. Click **"Process BOM & CPL"**. If you get a message about "TP1,TP3" designators missing, just click **continue**. These are test points and do not/should not have hardware placed.
9. On the Bill of Materials list, review the list to ensure that all expected parts are populated. For parts J2, J3, and J4, JLCPCB will **require you to check the box** to populate the part. If any parts are out of stock, you may need to either find a replacement (be conscious of the parts you are replacing!) or wait until they are restocked to order.
10. Click **"Next"** to proceed to the 3D assembly viewer, where you can see a rendering of what the board will look like after assembly, and double check postioning of parts
11. Click **"Next"** again to receive the final quote.
12. Choose a product description from the drop down box (I used `Household Appliance > Household Electric Heating Equipment - HS Code 851690`)
13. Click **"Add to Cart"** and then proceed to place the order **after reading the note below about M4 screws**.

**NOTE about M4 Screws** - JLCMC [sells](https://jlcmc.com/product/s/E02/EDLA/FA-%E7%B4%A7%E5%9B%BA%E9%9B%B6%E4%BB%B6-%E8%9E%BA%E9%92%89?productModelNumber=EDLA-S2-M4-L6) M4x6mm screws which they will ship alongside your PCB for a very reasonable price if ordered at the same time. I **highly** recommend ordering these together as the cost will likely be at least an order of magnitude lower than ordering separately from other sources. 

#### About the PCB

The PCB incorporates a number of components to make the build work:
- A 120V/240V AC to 48VDC converter to power the board & the repellers on the bus
- A 48VDC to 5VDC converter to power the microcontroller 
- Circuitry to high-side switch the +48VDC to the repellers to physically disconnect them from power when not in use
- MAX3485 RS-485 Transceivers to allow communication with the repellers

The PCB also adds two screw terminals that could be used in the future to add buttons to toggle each of the busses on/off. Although the headers for these are populated, support does not exist in the firmware and these are not otherwise referenced in these build instructions.

**NOTE** - If you edit the PCB in KiCad and use the JLCPCB tools to export the BoM/PnP files, you will need to manually edit the BoM and PnP files to add the second female pin header for the microcontroller (U2) as it will not be automatically added/placed.


### Microcontroller

The RepelBridge is controlled via a microcontroller which serves as the "brains" of your build. The PCB design is intended to work with:

- [Xiao ESP32-S3](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32S3-Pre-Soldered-p-6334.html)

The PCB is designed specifically for Seeed Studio's Xiao controllers. Seeed offers both pre-soldered and unsoldered versions of their boards - I recommend the pre-soldered version unless you are comfortable with a soldering iron.


### Other Components

In addition to the PCB, enclosure, and microcontroller there are a handful of other components required to build a RepelBridge -- especially given that we want it to remain waterproof.

- [AC power cable](https://www.amazon.com/dp/B07C9D6CXY) - You can select from a number of lengths, but make note of the gauge of the cable (16 vs. 14AWG). Larger gauge cable (higher number) will result in needing a larger cable gland
- [Cable gland](https://www.amazon.com/dp/B07DC7BH92) - I used a PG9 cable gland with a 16 AWG cable to keep the entry of my cable into the enclosure waterproof
- [Breather Vent](https://www.amazon.com/Dustproof-Waterproof-IP68-Breather-Screw/dp/B0DHGKN1TX) - A breather vent will prevent pressure differentials, while keeping your enclosure waterproof
- [1x or 2x M12 4-pin Type A Connectors](https://www.amazon.com/Waterproof-Connector-Bulkhead-Straight-Receptacle/dp/B0BVZDQYH5) - You will need one of these per bus you want to control
- [2x M4x6mm Screws](https://jlcmc.com/product/s/E02/EDLA/FA-%E7%B4%A7%E5%9B%BA%E9%9B%B6%E4%BB%B6-%E8%9E%BA%E9%92%89?productModelNumber=EDLA-S2-M4-L6) - For securing the PCB to the heat set inserts in the enclosure



## Assembly

Once you have received your microcontroller, enclosure, PCB, and other parts, you are ready to begin assembly. My recommendation for assembly order is:

1. Drill the holes in the enclosure
2. Attach the gland, vent, connectors, and power cable to the enclosure
3. Attach the PCB to the enclosure
4. Connect the wires to the PCB
5. Flash & connect the microcontroller to the PCB I recommend drilling the enclosure first, then attaching the gland/vent/connectors/power cable, connecting the PCB to the enclosure


### Drill the Enclosure

Before you beign, you will need to drill holes in your enclosure for the M12 connectors, cable gland, and vent:

- **M12 Connectors** - 16mm Clearance Holes
- **Vent** - 14mm Clearance Hole
- **Cable Gland** - 15.5mm (16mm) Clearance Hole

Although you can drill the holes in any order, I recommend that you place the M12 Connectors to the left of the case and the AC cable gland on the right. I have [a template](hardware/Enclosure%20Hole%20Template.pdf) in the hardware directory that you can print, cut out, and tape to the front of the enclosure to help act as a guide. If you do not plan to use both busses, you can omit one of the M12 connectors.

**TIP** - I recommend drilling a very small pilot hole at the "crosshairs" of the template before drilling the full size hole to help prevent the bit from "walking" across the face of the enclosure.

**NOTE** - The hole sizes listed above are very much approximate, as the drill bit I used - an imperial step drill bit - was absolutely not the right tool for the job. My research shows that the above are [likely the correct size](https://amesweb.info/Screws/Metric-Clearance-Hole-Chart.aspx) - if you try these and they work for you, please let me know.


### Assembling the Enclosure

After drilling the holes, insert the connectors, vent, and cable gland. Once the gland is secured to the enclosure, remove the "domed" front piece, and snake the AC power cable through the (loose) domed piece, and then through the rubber ring in the tines of the gland. Do not secure the domed piece until later, after you have connected the AC power to the PCB.


### Attach the PCB and Connect the Wires

Insert the PCB into the enclosure with the screw terminals facing the holes you just drilled. The PCB should be secured to the enclosure by using the 2 M4x6 screws. Screw the circuit board down into the brass heat set inserts on the left and right of the PCB using the appropriate hex key.

Once the PCB is secured, you are ready to connect the wires. Start by connecting the white and black wires from the AC power cable to the "AC IN" screw terminal on the far right side of the board:

- White Wire - Connects to "AC N"
- Black Wire - Connects to "AC L"
- Green Wire (if present) - Do not connect

**TIP** - It may be easier to secure the AC wires while the PCB is not screwed to the enclosure, and then re-secure it afterwards

After connecting the AC wiring you can slide the "domed" front piece of the cable gland up the cable and screw into the gland to tighten the rubber ring against the cable, which acts as both the waterproofing and as strain relief.

Next, you should connect the two M12 connectors. The wires to these connectors are typically color coded - assuming your connector has the standard colors, the typical wiring is:

- Brown wire - +48V
- Blue Wire - "A" Data Line
- Black Wire - "B" Data Line
- White Line - GND

**NOTE** - The wiring of the M12 connectors is _EXTREMELY_ important. Miswiring the connectors will burn out your repellers. 



### Flashing the Microcontroller

With your PCB wired, I recommend flashing the RepelBridge firmware to your microcontroller before plugging it in to the PCB.

**TODO** - Come back and write this section after adding RepelBridge to BrewFlasher. 


### Final Assembly

Plug the microcontroller into the female pin headers on the PCB with the USB-C port facing away from the enclosure wall - towards the screw terminals. Attach and secure the enclosure lid. Connect (and screw in) the connectors to the repellers - and plug your RepelBridge into the wall.

Congratulations - your RepelBridge is now built and ready to be set up. 


## Setup and Use

With your RepelBridge built, you will need to set it up to connect to WiFi and then in turn to Home Assistant.

**TODO** - Come back and write this section.

### Connecting to WiFi



### Installing the Home Assistant Integration



