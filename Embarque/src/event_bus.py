# src/event_bus.py
import threading

class EventBus:
    def __init__(self):
        self._subscribers = {}#dictionnary of the different sources and their callbacks example {"gps": [cb1, cb2], "imu": [cb3]}   
        self._lock = threading.Lock()#lock to protect the subscribers dictionary so that 2 subnscribers can't be added at the same time


    def subscribe(self, source: str, callback):
        with self._lock:#we lock the thread
            if source not in self._subscribers: #if the source is not in the subscribers dictionary we create an empty list for it
                self._subscribers[source] = []
            self._subscribers[source].append(callback)#we add the callback to the list of callbacks for the source

    def publish(self, source: str, data: dict):
        # On récupère les callbacks puis on libère le lock
        # pour ne pas bloquer le capteur pendant l'exécution
        with self._lock:
            callbacks = list(self._subscribers.get(source, []))
        
        # Chaque subscriber tourne dans son propre thread
        for cb in callbacks:
            threading.Thread(target=cb, args=(data,), daemon=True).start()

bus = EventBus()