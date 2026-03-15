class KloggError(RuntimeError):
    def __init__(self, code: str, message: str, payload=None):
        super().__init__(message or code)
        self.code = code
        self.message = message
        self.payload = payload or {}
