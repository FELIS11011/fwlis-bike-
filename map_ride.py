import pandas as pd
import folium

# ── Load your ride data ───────────────────────────────
df = pd.read_csv("ride.csv")
df = df.dropna(subset=["latitude", "longitude"])

# ── Color by comfort score ────────────────────────────
def comfort_color(score):
    if score >= 75: return "green"
    if score >= 50: return "orange"
    if score >= 25: return "red"
    return "darkred"

# ── Build map centered on your ride ───────────────────
center = [df["latitude"].mean(), df["longitude"].mean()]
m = folium.Map(location=center, zoom_start=16, tiles="OpenStreetMap")

# ── Draw each point ───────────────────────────────────
for _, row in df.iterrows():
    color = comfort_color(row["comfort"])
    folium.CircleMarker(
        location=[row["latitude"], row["longitude"]],
        radius=8,
        color=color,
        fill=True,
        fill_color=color,
        fill_opacity=0.85,
        popup=folium.Popup(
            f"<b>Point #{int(row['point'])}</b><br>"
            f"Time: {row['utc_time']}<br>"
            f"Speed: {row['speed_kmh']:.1f} km/h<br>"
            f"Comfort: {row['comfort']:.0f}/100<br>"
            f"Bumps: {int(row['bumps'])}<br>"
            f"RMS: {row['rms']:.3f}",
            max_width=200
        )
    ).add_to(m)

# ── Draw the route line ───────────────────────────────
coords = list(zip(df["latitude"], df["longitude"]))
folium.PolyLine(coords, color="blue", weight=2, opacity=0.5).add_to(m)

# ── Legend ────────────────────────────────────────────
legend = """
<div style="position:fixed; bottom:30px; left:30px; z-index:1000;
     background:white; padding:12px; border-radius:8px;
     border:1px solid grey; font-size:13px;">
  <b>Comfort Score</b><br>
  <span style="color:green">●</span> 75–100 Smooth<br>
  <span style="color:orange">●</span> 50–75  Moderate<br>
  <span style="color:red">●</span> 25–50  Rough<br>
  <span style="color:darkred">●</span> 0–25   Very rough
</div>
"""
m.get_root().html.add_child(folium.Element(legend))

m.save("ride_map.html")
print("Map saved as ride_map.html — open it in your browser!")