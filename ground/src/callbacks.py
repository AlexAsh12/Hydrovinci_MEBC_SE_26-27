from transmission import get_state, get_history, get_logs
from dash import Output, Input, callback, html
import plotly.graph_objects as go
from network_monitor import get_net_state 

# ─── Color helpers ────────────────────────────────────────────────────────────
COLORS = {
    "bg":       "#0d1117",
    "surface":  "#161b22",
    "border":   "#21262d",
    "text":     "#e6edf3",
    "muted":    "#8b949e",
    "green":    "#3fb950",
    "amber":    "#d29922",
    "red":      "#f85149",
    "blue":     "#388bfd",
    "cell_hi":  "#3B6D11",
    "cell_ok":  "#97C459",
    "cell_lo":  "#EF9F27",
    "cell_cr":  "#E24B4A",
}

PLOT_LAYOUT = dict(
    paper_bgcolor="rgba(0,0,0,0)",
    plot_bgcolor="rgba(0,0,0,0)",
    margin=dict(l=0, r=0, t=0, b=0),
    font=dict(family="'IBM Plex Mono', monospace", color=COLORS["muted"], size=11),
)


def cell_color(v, lo=3.15, hi=3.25):
    mid = (lo + hi) / 2
    if v >= hi:
        return COLORS["cell_hi"]
    if v >= mid:
        return COLORS["cell_ok"]
    if v >= lo:
        return COLORS["cell_lo"]
    return COLORS["cell_cr"]


def fmt_autonomy(remaining_ah, current_a):
    """Return human-readable autonomy string."""
    if current_a >= 0:
        return "∞ (charge)"
    hours = remaining_ah / abs(current_a)
    h = int(hours)
    m = int((hours - h) * 60)
    return f"{h}h {m:02d}min"


# ─── Callbacks ────────────────────────────────────────────────────────────────

@callback(
    Output("conn-dot",       "className"),
    Output("conn-label",     "children"),
    Output("last-update",    "children"),
    Output("net-indicator", "children"), 
    Output("net-indicator", "style"),  
    Input("interval","n_intervals"),
)
def update_status(n):
    s   = get_state()
    net = get_net_state()

    # MQTT
    cls   = "topbar-dot dot-on" if s["connected"] else "topbar-dot dot-off"
    label = "Connecté" if s["connected"] else "Déconnecté"
    ts    = f"Mis à jour : {s['last_update']}" if s["last_update"] else ""

    # Réseau
    icons = {
        "ok":       ("● Réseau OK",       "#3fb950"),
        "unstable": ("▲ Réseau instable", "#d29922"),
        "offline":  ("✕ Hors ligne",      "#f85149"),
        "checking": ("… Vérification",    "#8b949e"),
    }
    net_label, net_color = icons.get(net["status"], icons["checking"])

    if net["latency_avg"] and net["status"] != "offline":
        net_label += f" · {net['latency_avg']} ms"
    if net["loss_pct"] and net["status"] == "unstable":
        net_label += f" · {net['loss_pct']}% perte"

    net_style = {"color": net_color, "fontSize": "12px", "marginRight": "16px"}

    return cls, label, ts, net_label, net_style


@callback(
    Output("st-bms1", "className"),
    Output("st-bms2", "className"),
    Output("st-sim",  "className"),
    Output("st-sim-signal", "children"),
    Output("st-mqtt", "className"),
    Input("interval", "n_intervals"),
)
def update_statuts(n):
    s = get_state()
    conn = s.get("bms_connection", {})

    def dot(ok):
        return "topbar-dot dot-on" if ok else "topbar-dot dot-off"

    # Qualite signal (valeur AT+CSQ, ex: "14,99") -- affichee seulement si connue
    signal = s.get("sim_signal")
    signal_txt = f"CSQ {signal}" if signal else ""

    return (
        dot(conn.get("BMS_1")),
        dot(conn.get("BMS_2")),
        dot(s.get("sim_connection")),
        signal_txt,
        dot(s.get("connected")),     # MQTT = connexion du sol au broker
    )


@callback(
    Output("terminal",   "children"),
    Output("term-count", "children"),
    Input("interval",    "n_intervals"),
)
def update_terminal(n):
    logs = get_logs()

    def line_class(line):
        low = line.lower()
        if "erreur" in low or "error" in low or "traceback" in low or "exception" in low:
            return "term-line term-err"
        return "term-line"

    # Plus recent en haut : la derniere ligne est toujours visible sans scroller
    lignes = [html.Div(l, className=line_class(l)) for l in reversed(logs)]
    return lignes, f"{len(logs)} lignes"

@callback(
    Output("soc-gauge",      "figure"),
    Output("cap-total",      "children"),
    Output("cap-remaining",  "children"),
    Input("interval",        "n_intervals"),
)
def update_soc(n):
    s = get_state()
    soc = s["soc"]

    color = COLORS["green"] if soc > 40 else (COLORS["amber"] if soc > 20 else COLORS["red"])

    fig = go.Figure(go.Indicator(
        mode="gauge+number",
        value=soc,
        number=dict(suffix="%", font=dict(size=32, color=COLORS["text"],
                                          family="'IBM Plex Mono', monospace")),
        gauge=dict(
            axis=dict(range=[0, 100], visible=False),
            bar=dict(color=color, thickness=0.18),
            bgcolor="rgba(0,0,0,0)",
            borderwidth=0,
            steps=[dict(range=[0, 100], color=COLORS["border"])],
            threshold=dict(
                line=dict(color=COLORS["red"], width=2),
                thickness=0.75,
                value=20,
            ),
        ),
    ))


    fig.update_layout(
        paper_bgcolor="rgba(0,0,0,0)",
        plot_bgcolor="rgba(0,0,0,0)",
        font=dict(family="'IBM Plex Mono', monospace", color=COLORS["muted"], size=11),
        height=170,
        margin=dict(l=20, r=20, t=20, b=10),
    )
    return fig, f"{s['capacity_ah']:.0f}", f"{s['remaining_ah']:.1f}"
@callback(
    Output("pack-voltage",   "children"),
    Output("voltage-badge",  "children"),
    Output("voltage-badge",  "className"),
    Output("current-val",    "children"),
    Output("current-badge",  "children"),
    Output("current-badge",  "className"),
    Output("power-val",      "children"),
    Input("interval",        "n_intervals"),
)
def update_elec(n):
    s = get_state()
    v = s["pack_voltage"]
    i = s["current"]
    p = abs(v * i) / 1000

    # voltage badge
    if 46 <= v <= 54:
        v_badge, v_cls = "Nominal", "badge badge-ok"
    elif v < 46:
        v_badge, v_cls = "Bas", "badge badge-warn"
    else:
        v_badge, v_cls = "Élevé", "badge badge-warn"

    # current badge
    if i < 0:
        i_badge, i_cls = "Décharge", "badge badge-blue"
    else:
        i_badge, i_cls = "Charge", "badge badge-ok"

    return (
        f"{v:.1f}", v_badge, v_cls,
        f"{i:+.1f}", i_badge, i_cls,
        f"{p:.2f}",
    )


@callback(
    Output("speed-val",      "children"),
    Output("autonomy-val",   "children"),
    Output("temp-val",       "children"),
    Output("temp-badge",     "children"),
    Output("temp-badge",     "className"),
    Output("alarm-count",    "children"),
    Output("alarm-badge",    "children"),
    Output("alarm-badge",    "className"),
    Input("interval",        "n_intervals"),
)
def update_nav(n):
    s = get_state()
    temps = s["temperature"]
    if isinstance(temps, list):
        tmax = max(temps) if temps else 0.0
    else:
        tmax = temps

    if tmax < 35:
        t_badge, t_cls = "OK", "badge badge-ok"
    elif tmax < 45:
        t_badge, t_cls = "Chaud", "badge badge-warn"
    else:
        t_badge, t_cls = "Critique", "badge badge-err"

    alarms = s["alarms"]
    n_alarms = len(alarms)
    a_badge = "Clear" if n_alarms == 0 else f"{n_alarms} alarme{'s' if n_alarms > 1 else ''}"
    a_cls   = "badge badge-ok" if n_alarms == 0 else "badge badge-err"

    auto = fmt_autonomy(s["remaining_ah"], s["current"])

    return (
        f"{s['speed_knots']:.1f}",
        auto,
        f"{tmax:.1f}", t_badge, t_cls,
        str(n_alarms), a_badge, a_cls,
    )


@callback(
    Output("cell-bar",   "figure"),
    Output("cell-delta", "children"),
    Input("interval",    "n_intervals"),
)
def update_cells(n):
    s = get_state()
    cells = s["cell_voltages"]
    labels = [f"C{i+1}" for i in range(len(cells))]
    colors = [cell_color(v) for v in cells]

    fig = go.Figure(go.Bar(
        x=labels,
        y=cells,
        marker_color=colors,
        marker_line_width=0,
    ))
    if cells:
        y_min = min(3.0, min(cells) - 0.05)
        y_max = max(3.4, max(cells) + 0.05)
        delta = f"min {min(cells):.3f} V · max {max(cells):.3f} V · Δ {max(cells)-min(cells):.3f} V"
    else:
        y_min, y_max = 3.0, 3.4
        delta = ""
    fig.update_layout(
        **PLOT_LAYOUT,
        height=160,
        xaxis=dict(showgrid=False, tickfont=dict(size=10)),
        yaxis=dict(
            range=[y_min, y_max],
            showgrid=True,
            gridcolor=COLORS["border"],
            tickformat=".2f",
            ticksuffix="V",
        ),
        bargap=0.25,
    )
    return fig, delta


@callback(
    Output("cell-pie", "figure"),
    Input("interval",  "n_intervals"),
)
def update_pie(n):
    s = get_state()
    cells = s["cell_voltages"]
    labels = [f"C{i+1}" for i in range(len(cells))]

    PALETTE = [
        "#185FA5","#1D9E75","#639922","#BA7517","#993C1D",
        "#534AB7","#0F6E56","#3B6D11","#854F0B","#993556",
        "#378ADD","#5DCAA5","#97C459","#EF9F27","#D85A30",
    ]

    fig = go.Figure(go.Pie(
        labels=labels,
        values=cells,
        hole=0.55,
        marker=dict(colors=PALETTE[:len(cells)], line=dict(color=COLORS["bg"], width=1.5)),
        textinfo="none",
        hovertemplate="%{label}: %{value:.3f} V<extra></extra>",
    ))
    fig.update_layout(
        **PLOT_LAYOUT,
        height=220,
        showlegend=False,
    )
    return fig


@callback(
    Output("cap-history",   "figure"),
    Output("history-range", "children"),
    Input("interval",       "n_intervals"),
)
def update_cap_history(n):
    hist = get_history()

    if not hist:
        fig = go.Figure()
        fig.update_layout(**PLOT_LAYOUT, height=220)
        return fig, ""

    times = [h["t"] for h in hist]
    ahs   = [h["ah"] for h in hist]

    fig = go.Figure(go.Scatter(
        x=times, y=ahs,
        mode="lines",
        line=dict(color=COLORS["blue"], width=2),
        fill="tozeroy",
        fillcolor="rgba(56,139,253,0.08)",
        hovertemplate="%{x} · %{y:.1f} Ah<extra></extra>",
    ))
    fig.update_layout(
        **PLOT_LAYOUT,
        height=220,
        xaxis=dict(showgrid=False, tickfont=dict(size=10), nticks=6),
        yaxis=dict(
            showgrid=True,
            gridcolor=COLORS["border"],
            ticksuffix=" Ah",
        ),
    )
    hint = f"{times[0]} → {times[-1]}" if len(times) > 1 else ""
    return fig, hint
