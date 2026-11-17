from dash import Dash

app = Dash(
    __name__,
    title="BMS Dashboard",
    update_title=None,
    suppress_callback_exceptions=True,
)
server = app.server
