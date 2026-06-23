from dash import dcc, html

REFRESH_MS = 1000  # UI refresh interval in milliseconds

_EMPTY_FIG = {"layout": {
    "paper_bgcolor": "rgba(0,0,0,0)",
    "plot_bgcolor":  "rgba(0,0,0,0)",
    "xaxis": {"visible": False},
    "yaxis": {"visible": False},
}}

def _status_row(label, dot_id):
    """Une ligne de statut : pastille coloree + libelle."""
    return html.Div(className="status-item", children=[
        html.Span(id=dot_id, className="topbar-dot dot-off"),
        html.Span(label, className="status-label"),
    ])


def build_layout():
    return html.Div(
        className="dash-root",
        children=[
            dcc.Interval(id="interval", interval=REFRESH_MS, n_intervals=0),

            # --- Top bar ---
            html.Div(className="topbar", children=[
                html.Div(className="topbar-left", children=[
                    html.Div(className="topbar-dot", id="conn-dot"),
                    html.Span("BMS Dashboard", className="topbar-title"),
                    html.Span(id="conn-label", className="topbar-status"),
                ]),
                html.Div(className="topbar-right", children=[
                    html.Span(id="last-update", className="topbar-ts"),
                    html.Span(id="net-indicator", className="topbar-status"),
                ]),
            ]),

            # --- Row 1 : SOC | Electrique | Navigation ---
            html.Div(className="row row-3", children=[

                # SOC card
                html.Div(className="card card-soc", children=[
                    html.Div("State of charge", className="card-label"),
                    html.Div(className="soc-ring-wrap", children=[
                        dcc.Graph(id="soc-gauge", figure=_EMPTY_FIG, config={"displayModeBar": False},
                                  className="soc-graph",
                                  style={"height": "180px"}),
                    ]),
                    html.Div(className="soc-stats", children=[
                        html.Div(className="soc-stat", children=[
                            html.Div("Capacité", className="card-label"),
                            html.Span(id="cap-total", className="stat-val"),
                            html.Span(" Ah", className="stat-unit"),
                        ]),
                        html.Div(className="soc-divider"),
                        html.Div(className="soc-stat", children=[
                            html.Div("Restante", className="card-label"),
                            html.Span(id="cap-remaining", className="stat-val"),
                            html.Span(" Ah", className="stat-unit"),
                        ]),
                    ]),
                ]),

                # Electrical card
                html.Div(className="card", children=[
                    html.Div("Électrique", className="card-label"),
                    html.Div(className="elec-row", children=[
                        html.Div(className="elec-item", children=[
                            html.Div("Tension globale", className="card-label"),
                            html.Div(className="elec-val-row", children=[
                                html.Span(id="pack-voltage", className="big-val"),
                                html.Span(" V", className="big-unit"),
                                html.Span(id="voltage-badge", className="badge"),
                            ]),
                        ]),
                        html.Div(className="sep"),
                        html.Div(className="elec-item", children=[
                            html.Div("Courant", className="card-label"),
                            html.Div(className="elec-val-row", children=[
                                html.Span(id="current-val", className="big-val"),
                                html.Span(" A", className="big-unit"),
                                html.Span(id="current-badge", className="badge"),
                            ]),
                        ]),
                        html.Div(className="sep"),
                        html.Div(className="elec-item", children=[
                            html.Div("Puissance", className="card-label"),
                            html.Div(className="elec-val-row", children=[
                                html.Span(id="power-val", className="big-val"),
                                html.Span(" kW", className="big-unit"),
                            ]),
                        ]),
                    ]),
                ]),

                # Navigation card
                html.Div(className="card", children=[
                    html.Div("Navigation & état", className="card-label"),
                    html.Div(className="elec-row", children=[
                        html.Div(className="elec-item", children=[
                            html.Div("Vitesse", className="card-label"),
                            html.Div(className="elec-val-row", children=[
                                html.Span(id="speed-val", className="big-val"),
                                html.Span(" kn", className="big-unit"),
                            ]),
                        ]),
                        html.Div(className="sep"),
                        html.Div(className="elec-item", children=[
                            html.Div("Autonomie estimée", className="card-label"),
                            html.Span(id="autonomy-val", className="big-val"),
                            html.Div("à vitesse constante", className="sub-hint"),
                        ]),
                        html.Div(className="sep"),
                        html.Div(className="nav-bottom", children=[
                            html.Div(className="elec-item", children=[
                                html.Div("Temp. max", className="card-label"),
                                html.Div(className="elec-val-row", children=[
                                    html.Span(id="temp-val", className="med-val"),
                                    html.Span(" °C", className="big-unit"),
                                    html.Span(id="temp-badge", className="badge"),
                                ]),
                            ]),
                            html.Div(className="elec-item", children=[
                                html.Div("Alarmes", className="card-label"),
                                html.Div(className="elec-val-row", children=[
                                    html.Span(id="alarm-count", className="med-val"),
                                    html.Span(id="alarm-badge", className="badge", style={"marginLeft": "6px"}),
                                ]),
                            ]),
                        ]),
                    ]),
                ]),
            ]),

            # --- Row 2 : Cell voltages bar chart ---
            html.Div(className="card", children=[
                html.Div(className="card-header", children=[
                    html.Span("Tension par cellule — 15 cellules", className="card-htitle"),
                    html.Span(id="cell-delta", className="card-hint"),
                ]),
                dcc.Graph(id="cell-bar", figure=_EMPTY_FIG, config={"displayModeBar": False},
                          style={"height": "160px"}),
                html.Div(className="legend", children=[
                    html.Span(className="legend-item", children=[
                        html.Span(className="legend-dot", style={"background": "#3B6D11"}),
                        "Élevé",
                    ]),
                    html.Span(className="legend-item", children=[
                        html.Span(className="legend-dot", style={"background": "#97C459"}),
                        "Nominal",
                    ]),
                    html.Span(className="legend-item", children=[
                        html.Span(className="legend-dot", style={"background": "#EF9F27"}),
                        "Bas",
                    ]),
                    html.Span(className="legend-item", children=[
                        html.Span(className="legend-dot", style={"background": "#E24B4A"}),
                        "Critique",
                    ]),
                ]),
            ]),

            # --- Row 3 : Pie + Capacity history ---
            html.Div(className="row row-2", children=[
                html.Div(className="card", children=[
                    html.Div(className="card-header", children=[
                        html.Span("Répartition des cellules", className="card-htitle"),
                    ]),
                    dcc.Graph(id="cell-pie", figure=_EMPTY_FIG, config={"displayModeBar": False},
                              style={"height": "220px"}),
                ]),
                html.Div(className="card", children=[
                    html.Div(className="card-header", children=[
                        html.Span("Capacité résiduelle", className="card-htitle"),
                        html.Span(id="history-range", className="card-hint"),
                    ]),
                    dcc.Graph(id="cap-history", figure=_EMPTY_FIG, config={"displayModeBar": False},
                              style={"height": "220px"}),
                ]),
            ]),

            # --- Row 4 : Statuts systeme + Terminal Pi temps reel ---
            html.Div(className="row row-term", children=[
                html.Div(className="card", children=[
                    html.Div(className="card-header", children=[
                        html.Span("Statuts système", className="card-htitle"),
                    ]),
                    html.Div(className="status-list", children=[
                        _status_row("BMS_1",   "st-bms1"),
                        _status_row("BMS_2",   "st-bms2"),
                        # SIM7600 : pastille + libelle + valeur AT+CSQ (qualite signal)
                        html.Div(className="status-item", children=[
                            html.Span(id="st-sim", className="topbar-dot dot-off"),
                            html.Span("SIM7600", className="status-label"),
                            html.Span(id="st-sim-signal", className="status-signal"),
                        ]),
                        _status_row("MQTT",    "st-mqtt"),
                    ]),
                ]),
                html.Div(className="card", children=[
                    html.Div(className="card-header", children=[
                        html.Span("Terminal Pi — temps réel", className="card-htitle"),
                        html.Span(id="term-count", className="card-hint"),
                    ]),
                    html.Div(id="terminal", className="terminal"),
                ]),
            ]),
        ],
    )
