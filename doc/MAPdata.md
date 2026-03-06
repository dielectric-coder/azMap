# Map Data Layers

azMap provides three real-time propagation overlay layers, toggled via buttons in the LAYERS section of the sidebar. All layers auto-refresh every 15 minutes.

---

## MUF (Maximum Usable Frequency)

**Data source:** Pre-rendered GeoJSON contour lines from [KC2G](https://prop.kc2g.com/) (`mufd-normal-now.geojson`), derived from a global network of ionosonde stations.

**What it shows:** Contour lines of the Maximum Usable Frequency for a 3000 km path (MUF(3000)). This is the highest frequency that will be refracted back to Earth by the F2 layer of the ionosphere over a typical HF skip distance.

**How to read it:**

- Each contour line is labeled with a frequency in MHz and drawn in a distinct color
- The MUF value applies to a ~3000 km oblique path, not vertical incidence
- **If your operating frequency is below the MUF** along your path, F2 propagation is likely supported
- **If your operating frequency is above the MUF**, signals will pass through the ionosphere instead of being reflected — no propagation via that layer
- Higher MUF values (warm colors) indicate stronger ionization, typically on the sunlit side of the Earth
- MUF follows a diurnal cycle: it rises after sunrise, peaks in the afternoon, and drops after sunset
- Seasonal variation: MUF tends to be higher during the equinoxes and solar maximum years

**Practical use:**

Look at the MUF contour values along the midpoint of the path between you and your target. If you're operating on 14 MHz and the MUF at the midpoint is 18 MHz, the band should be open. If the MUF has dropped to 10 MHz, 20m is likely dead for that path but 30m/40m may still work.

---

## Sporadic E (foEs)

**Data source:** Raw ionosonde station readings from [KC2G](https://prop.kc2g.com/) (`stations.json`), interpolated to a grid using Inverse Distance Weighting (IDW) and contoured via marching squares.

**What it shows:** Contour lines of foEs — the critical frequency of Sporadic E layers. Sporadic E (Es) are transient, patchy clouds of intense ionization in the E layer at ~100 km altitude that can reflect signals well above the normal E layer MUF.

**Contour levels:**

| Color  | foEs  | Approximate oblique MUF | Bands supported           |
|--------|-------|-------------------------|---------------------------|
| Blue   | 3 MHz | ~10–12 MHz              | 30m and below             |
| Green  | 5 MHz | ~15–20 MHz              | 20m–15m                   |
| Yellow | 7 MHz | ~21–28 MHz              | 15m–10m                   |
| Orange | 10 MHz| ~40–50 MHz              | 6m openings               |
| Red    | 14 MHz| ~56–100+ MHz            | 6m, possibly 2m           |

**Key concepts:**

- **foEs vs. oblique MUF:** foEs is the vertical-incidence critical frequency. At the oblique angles of a typical skip path, Es can support propagation up to roughly 4–5× the foEs value (the "MUF factor")
- **Es is patchy:** Unlike F2 propagation which blankets large areas, Es clouds are localized (a few hundred km across). The contour lines show interpolated regions, but actual Es patches may be smaller and more intense than depicted
- **Seasonal:** Es is most common during summer months (May–August in the Northern Hemisphere, November–February in the Southern Hemisphere). Winter Es exists but is much weaker and less frequent
- **Station coverage:** Contours are interpolated from ~40–60 ionosonde stations worldwide, with best coverage over Europe, North America, and Australia. Gaps between stations may miss localized Es events

**How to read it:**

- Contour lines enclose regions where foEs is **at or above** that level
- For a skip path to work via Es, the Es cloud needs to be **along your propagation path** (near the reflection midpoint), not necessarily directly over your station
- If you see only the blue 3 MHz contour, Es activity is minimal
- Yellow/orange/red contours indicate strong Es events — check 10m and 6m for openings

**Open vs. closed contour lines:**

You will see a mix of closed loops and open lines that seem to end abruptly — both are normal:

- **Closed contours** — the Es region is fully enclosed within the ionosonde coverage area (e.g., a green oval over India or a blue loop over Europe)
- **Open contours** — the contour line runs into the edge of the data coverage area and stops. The interpolation uses a 2500 km influence radius from each ionosonde station; beyond that, there is no data to continue the line

This is analogous to weather maps where isobars sometimes close into circles around pressure centers and sometimes run off the edge of the map. Open contours are especially common because ionosonde coverage is sparse — large gaps exist over oceans, Africa, and central Asia where no stations are present, so the interpolation simply cuts off at the radius limit

---

## Aurora

**Data source:** NOAA/SWPC Ovation Aurora model (`ovation_aurora_latest.json`), providing aurora probability on a 1° grid. Geomagnetic indices (Kp and Bz) are fetched separately.

**What it shows:** A green heatmap overlay showing the probability of visible aurora (and by extension, auroral propagation conditions). Brighter green = higher aurora probability. When active, Kp and Bz indices are displayed in the sidebar legend.

**How to read it:**

- The green glow indicates where the auroral oval is currently active
- **Kp index** (0–9): A measure of global geomagnetic disturbance. Higher Kp = more disturbed geomagnetic field = larger auroral oval extending to lower latitudes
  - Kp 0–2: Quiet, aurora confined to high latitudes
  - Kp 3–4: Unsettled, aurora visible at ~60° latitude
  - Kp 5+: Storm conditions, aurora may reach 50° or lower
- **Bz component** (nT): The north-south component of the Interplanetary Magnetic Field (IMF)
  - **Bz negative (southward):** Energy transfers into Earth's magnetosphere more efficiently — auroral activity increases. More negative = stronger effect
  - **Bz positive (northward):** Geomagnetic coupling is weaker — quieter conditions

**Propagation implications:**

- **HF:** Auroral conditions generally **degrade** HF propagation on polar and trans-polar paths. Signals passing through the auroral zone suffer absorption, rapid fading, and flutter. During geomagnetic storms, polar paths may black out entirely while lower-latitude paths remain usable
- **VHF:** Aurora can **enable** VHF propagation via auroral scatter (signals reflected off the ionized auroral curtain). CW and digital modes work best — SSB signals become distorted with a characteristic "buzz" due to the rapidly moving reflectors
- **Polar path assessment:** If your great-circle path to a target crosses through the green auroral zone, expect degraded HF conditions. Consider alternative paths or wait for quieter conditions

---

## Using layers together

The three layers can be activated simultaneously:

- **MUF + Sporadic E:** See both the "baseline" F2 propagation and any Es enhancement. Useful for determining whether an opening on 10m or 6m is due to F2 or Es
- **MUF + Aurora:** Identify paths where the MUF is nominally high enough but auroral absorption may kill the signal. If your path crosses the green zone, conditions may be worse than the MUF alone suggests
- **All three:** Complete situational awareness for HF/VHF propagation assessment
