"""Filesystem ownership for the MD01c proof; no product settings or captures."""

import logging
import os
import stat
import tempfile
from pathlib import Path

from message_decoder_pdu import INPUT_LIMIT, ProofError, load_manifest

LOG = logging.getLogger("cilogg.decoder.files")


def signature(info):
    # Windows path-stat/descriptor-stat expose different ctime interpretations.
    return (
        info.st_dev,
        info.st_ino,
        info.st_size,
        info.st_mtime_ns,
        info.st_ctime_ns if os.name != "nt" else None,
    )


class OriginInputs:
    """Read each input once and retain immutable bytes plus source identities."""

    def __init__(self, manifest):
        self.records = []
        encoded = self.read(Path(manifest), INPUT_LIMIT)
        self.cases = load_manifest(
            self.records[0][1],
            manifest_bytes=encoded,
            read_source=lambda path, expected, limit: self.read(path, limit, expected),
        )
        self.validate()
        LOG.info(
            "origin_inputs_frozen files=%d cases=%d", len(self.records), len(self.cases)
        )

    def read(self, path, limit, expected=None):
        resolved = path.resolve()
        if expected is not None and expected != resolved:
            raise ProofError("input_changed")
        try:
            before = resolved.stat()
            if not stat.S_ISREG(before.st_mode):
                raise ProofError("input_not_regular")
            flags = (
                os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NONBLOCK", 0)
            )
            # Nonblocking open prevents a regular-file -> FIFO race from hanging.
            descriptor = os.open(resolved, flags)
            with os.fdopen(descriptor, "rb") as source:
                opened = os.fstat(source.fileno())
                if not stat.S_ISREG(opened.st_mode):
                    raise ProofError("input_not_regular")
                if signature(before) != signature(opened):
                    raise ProofError("input_changed")
                if opened.st_size > limit:
                    raise ProofError("input_limit")
                encoded = source.read(limit + 1)
                after = os.fstat(source.fileno())
            if len(encoded) > limit:
                raise ProofError("input_limit")
            if signature(opened) != signature(after):
                raise ProofError("input_changed")
            self.records.append((path, resolved, signature(after)))
            self.validate()
            LOG.debug(
                "origin_input_frozen bytes=%d file_index=%d",
                len(encoded),
                len(self.records) - 1,
            )
            return encoded
        except FileNotFoundError as error:
            raise ProofError("input_missing") from error

    def validate(self):
        try:
            for path, resolved, frozen in self.records:
                if path.resolve() != resolved or signature(resolved.stat()) != frozen:
                    raise ProofError("input_changed")
        except OSError as error:
            raise ProofError("input_changed") from error

    def ensure_safe(self, destination, extra_inputs):
        self.validate()
        protected = (
            [path for path, _, _ in self.records]
            + [resolved for _, resolved, _ in self.records]
            + list(extra_inputs)
        )
        for path in protected:
            if destination.resolve() == path.resolve() or (
                destination.exists() and path.exists() and destination.samefile(path)
            ):
                raise ProofError("invalid_origin_destination")


class OutputLease:
    """Nonblocking OS lock; persistent lock inode avoids unlink/recreate races."""

    def __init__(self, directory, inputs, extra_inputs):
        self.directory = Path(directory).resolve()
        self.inputs, self.extra_inputs = inputs, extra_inputs
        self.descriptor = None

    def __enter__(self):
        self.directory.mkdir(parents=True, exist_ok=True)
        path = self.directory / ".decoder-origins.lock"
        self.inputs.ensure_safe(path, self.extra_inputs)
        if path.is_symlink() or (
            path.exists() and not stat.S_ISREG(path.stat().st_mode)
        ):
            raise ProofError("invalid_output_lease")
        flags = (
            os.O_RDWR
            | os.O_CREAT
            | getattr(os, "O_NONBLOCK", 0)
            | getattr(os, "O_NOFOLLOW", 0)
        )
        descriptor = os.open(path, flags, 0o600)
        try:
            if not stat.S_ISREG(os.fstat(descriptor).st_mode):
                raise ProofError("invalid_output_lease")
            # Recheck an existing inode before acquiring a lease on it.
            self.inputs.ensure_safe(path, self.extra_inputs)
            try:
                if os.name == "nt":
                    import msvcrt

                    msvcrt.locking(descriptor, msvcrt.LK_NBLCK, 1)
                else:
                    import fcntl

                    fcntl.flock(descriptor, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except OSError as error:
                raise ProofError("output_busy") from error
            self.descriptor = descriptor
            LOG.info("origin_output_lease_acquired")
            return self
        except BaseException:
            os.close(descriptor)
            raise

    def __exit__(self, *exception):
        os.close(self.descriptor)
        self.descriptor = None
        LOG.info("origin_output_lease_released")


def publish_report(destination, data, inputs, extra_inputs):
    """Validate frozen sources then atomically replace this invocation's report."""
    inputs.ensure_safe(destination, extra_inputs)
    descriptor, temporary = tempfile.mkstemp(
        prefix=".origin-report-", suffix=".tmp", dir=destination.parent
    )
    temporary = Path(temporary)
    try:
        with os.fdopen(descriptor, "wb") as output:
            output.write(data)
            output.flush()
        # Never reread the manifest or fixtures; reject observed source changes.
        inputs.ensure_safe(destination, extra_inputs)
        os.replace(temporary, destination)
        LOG.info("origin_report_published bytes=%d", len(data))
    finally:
        temporary.unlink(missing_ok=True)
