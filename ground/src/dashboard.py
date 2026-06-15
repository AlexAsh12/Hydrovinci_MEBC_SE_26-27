from app import app
from layout import build_layout
import data_logger 
import callbacks  # noqa: F401 ÔÇö registers all @callback decorators
app.layout = build_layout()

if __name__ == "__main__":
    app.run(
        host="0.0.0.0",   # accessible sur le r├®seau local
        port=8050,
        debug=False,
    )
