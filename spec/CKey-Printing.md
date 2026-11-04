# Printing CKey symbols (informative, draft 0.1)

Small prints fail for three reasons, in this order:

1. **Marks thinner than the printer can draw.**
2. **Grey anti-aliased edges** that the printer halftones into noise.
3. **Ink spreading into the gaps.**

The tools address all three.

## Rules

- **Print dark marks on paper.** The default screen palette (cream marks on black) inks the background, so ink spread eats thin light marks. In simulation, dark-on-paper prints about 35% smaller on plain inkjet paper.
- **Use the print layout for small sizes: Profile 0.1** (`--layout print`, Format 0.28). Every mark and gap is at least about one module, so the same payload prints at roughly half Profile 0's width.
- **Print a device-grid bitmap at 100% / actual size.** `ckey encode-scalable … --png file.png --size-in 0.5 --dpi 600`, or the CKey Renderer's **Export Print PNG…**, writes black-or-white dots at the printer's resolution with no grey, and records the dpi so it prints at true size. Don't let the print dialog scale it ("fit to page").
- **Paper matters as much as the printer.** Coated or photo paper keeps dots sharp. Plain paper spreads ink.
- **Leave the quiet margin.** The rendered canvas already includes it; don't crop it off.

## What to expect (simulation)

Smallest width that decodes reliably with Profile 0.1 at level M. The camera is 23 px/mm, roughly a phone at about 12 cm in 4K. A weaker camera needs about 1.3–1.5× these sizes.

| Payload | Old inkjet, plain paper | Inkjet, photo paper | 600 dpi laser* |
|---|---|---|---|
| 8 B | 0.19 in | ≤ 0.15 in | — |
| 30 B (short URL) | 0.24 in | ≤ 0.15 in | — |
| 120 B | 0.32 in | 0.21 in | — |
| 400 B | 0.56 in | 0.38 in | — |
| 1,200 B | 0.91 in | 0.56 in | — |
| 2,400 B | 1.34 in | 0.83 in | — |

\*Laser figures were measured only for Profile 0 (0.31 in at 8 B to 2.16 in at about 4.6 KB). Laser behaves between plain inkjet and photo paper.

**Hard limits.** No layout can beat the dots a printer can place, or the pixels a camera can resolve. Profile 0's densest version (232 modules across) at 1 inch needs 0.10 mm modules and 0.02 mm rays, finer than a 600 dpi printer dot. For data that dense at that size, use Profile 0.1 with a lower version, or print larger.

## Calibrating your printer

```sh
./build/ckey print-sheet --out sheet.png             # 8–400 B at 0.25–1.0 in
./build/ckey print-sheet --set large --out big.png   # 1,200 and 2,400 B at 1.0–2.0 in
```

1. **Print** each sheet at 100% / actual size on the paper you intend to use.
2. **Scan each cell.** Every cell's payload names itself, for example `CKEY-CAL P0.1 120B 0.50in`. The 8-byte cells only hold `CKEY-CAL`; use their printed labels.
3. **Read off your limit:** the smallest cell that scans in each row is the real limit for your printer, paper and camera.
