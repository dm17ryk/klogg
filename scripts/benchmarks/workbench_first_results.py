"""First-useful-page observation, distinct from completed-job observation (G02i)."""

import logging
import os
import shutil
import sys
import time
from contextlib import ExitStack
from time import perf_counter as clock

from workbench_runtime import RunError, file_hash
from workbench_viewer import (
    ViewerHost,
    fixture,
    prepare_payload,
    private_ipc_environment,
    require_operations,
    wait_state,
)

LOG = logging.getLogger("workbench.first_results")
TOKEN = "G02I_USEFUL"
REVISION = "9007199254740993"
FIRST_RESULT_BUDGETS = dict(
    firstResultMs=10000.0, completionMs=60000.0, openIndexSeconds=30.0
)


def require(condition, reason):
    if not condition:
        LOG.error("first_result_rejected reason=%s", reason)
        raise RunError(reason)


def remaining(deadline, reason):
    value = deadline - clock()
    require(value > 0, reason)
    return min(15, value)


class FirstResultsHost(ViewerHost):
    """Give the startup read its remaining existing30s allowance, without retry."""

    def start(self):
        self._startup_read = True
        try:
            super().start()
        finally:
            self._startup_read = False

    def remaining(self, maximum=None):
        startup = getattr(self, "_startup_read", False)
        deadline = self.deadline
        if startup and self.started is not None:
            deadline = min(deadline, self.started + 30)
        left = deadline - clock()
        require(
            left > 0,
            "first_result_startup_timeout" if startup else "first_result_host_timeout",
        )
        return min((30 if startup else 15) if maximum is None else maximum, left)

    def command(self, action, arguments=None, timeout=15, allow_error=False):
        if action == "get_info" and getattr(self, "_startup_read", False):
            timeout = self.remaining()
            LOG.debug("first_result_startup_read remaining_seconds=%.6f", timeout)
        return super().command(
            action, arguments, timeout=timeout, allow_error=allow_error
        )


def counter(value):
    require(
        type(value) is int
        or (
            isinstance(value, str)
            and value.isascii()
            and value.isdigit()
            and len(value) <= 20
        ),
        "first_result_invalid_counter",
    )
    result = int(value)
    require(0 <= result < 2**64, "first_result_invalid_counter")
    return result


def verify_snapshot(status, job, size, source=None):
    require(isinstance(status, dict), "first_result_snapshot")
    metadata = status.get("source")
    require(
        isinstance(metadata, dict)
        and status.get("jobId") == job
        and status.get("requestRevision") == REVISION
        and isinstance(metadata.get("sourceId"), str)
        and bool(metadata["sourceId"])
        and isinstance(metadata.get("generation"), str)
        and counter(metadata.get("indexedBytes")) == size
        and (source is None or metadata == source),
        "first_result_snapshot",
    )
    counter(metadata["generation"])
    require(
        status.get("state")
        in ("queued", "running", "completed", "failed", "cancelled"),
        "first_result_snapshot",
    )
    return metadata


def verify_useful_row(row, job, source):
    group = dict(
        number=0,
        name="",
        matched=True,
        value=TOKEN,
        startUtf16=0,
        lengthUtf16=len(TOKEN),
    )
    match = dict(startUtf16=0, lengthUtf16=len(TOKEN), groups=[group])
    expected = dict(
        jobId=job,
        requestRevision=REVISION,
        source=source,
        lineNumber=1,
        text=TOKEN,
        matched=True,
        offsetConvention="zero-based UTF-16",
        subpatterns=[dict(index=0, matched=True, matches=[match])],
    )
    require(
        row == expected
        and type(row.get("lineNumber")) is int
        and row.get("matched") is True,
        "first_result_row_oracle",
    )
    # JSON booleans compare equal to integers in Python; preserve the public types.
    pattern = row["subpatterns"][0]
    value = pattern["matches"][0]
    require(
        type(pattern["index"]) is int
        and pattern["matched"] is True
        and value["groups"][0]["matched"] is True
        and all(
            type(obj[key]) is int
            for obj in (value, value["groups"][0])
            for key in ("startUtf16", "lengthUtf16")
        )
        and type(value["groups"][0]["number"]) is int,
        "first_result_row_oracle",
    )


def observe_first_results(host, size, rows, budgets, record, outer_deadline):
    """Measure delivered/validated data; never infer readiness from submission."""
    record.update(complete=False, phase="submitting", pagePolls=0, emptyPages=0)
    started = clock()
    deadline = min(outer_deadline, started + budgets["completionMs"] / 1000)
    first_deadline = min(deadline, started + budgets["firstResultMs"] / 1000)
    reply = host.command(
        "regex_evaluate",
        {
            "analysis_json": dict(
                scope="whole_file",
                pattern="^" + TOKEN + "$",
                requestRevision=REVISION,
                limits={"timeoutMs": min(600000, max(1, int(budgets["completionMs"])))},
            )
        },
        timeout=remaining(first_deadline, "first_result_timeout"),
    )
    job = reply.get("jobId")
    require(isinstance(job, str) and 0 < len(job) <= 128, "first_result_submit_failed")
    host.jobs.add(job)
    record.update(
        jobId=job,
        requestRevision=REVISION,
        submissionAcknowledgedMs=(clock() - started) * 1000,
    )
    remaining(first_deadline, "first_result_timeout")
    status = host.command(
        "get_analysis_job",
        {"job_id": job},
        timeout=remaining(first_deadline, "first_result_timeout"),
    )
    source = verify_snapshot(status, job, size)
    record.update(source=dict(source), phase="awaiting_first_page")
    LOG.info(
        "first_result_snapshot job_id=%s source_id=%s generation=%s watermark=%s",
        job,
        source["sourceId"],
        source["generation"],
        source["indexedBytes"],
    )
    useful = False
    polls = 0
    while True:
        polls += 1
        require(polls <= 4096, "first_result_poll_limit")
        phase_deadline = deadline if useful else first_deadline
        reason = "first_result_completion_timeout" if useful else "first_result_timeout"
        remaining(phase_deadline, reason)
        require(
            status["state"] not in ("failed", "cancelled"), "first_result_job_failed"
        )
        completed_before_page = status["state"] == "completed"
        if completed_before_page and "completedJobStatusObservedMs" not in record:
            record["completedJobStatusObservedMs"] = (clock() - started) * 1000
        if not useful:
            requested = clock()
            page = host.command(
                "get_analysis_results",
                {"job_id": job, "page_size": 1},
                timeout=remaining(first_deadline, "first_result_timeout"),
            )
            observed = clock()
            record["pagePolls"] += 1
            remaining(first_deadline, "first_result_timeout")
            require(
                isinstance(page, dict)
                and page.get("jobId") == job
                and page.get("requestRevision") == REVISION
                and page.get("source") == source,
                "first_result_page_provenance",
            )
            page_rows = page.get("rows")
            require(
                isinstance(page_rows, list) and len(page_rows) <= 1,
                "first_result_page_shape",
            )
            LOG.debug(
                "first_result_poll job_id=%s polls=%d state=%s page_rows=%d",
                job,
                record["pagePolls"],
                page.get("state"),
                len(page_rows),
            )
            if page_rows:
                verify_useful_row(page_rows[0], job, source)
                useful = True
                record.update(
                    firstUsefulPageObservedMs=(observed - started) * 1000,
                    firstUsefulPageRequestStartedMs=(requested - started) * 1000,
                    firstUsefulPageQueryMs=(observed - requested) * 1000,
                    firstUsefulPageState=page.get("state"),
                    firstUsefulPhysicalLine=1,
                    phase="awaiting_completion",
                )
                LOG.info(
                    "first_result_validated job_id=%s observed_ms=%.3f line=1",
                    job,
                    record["firstUsefulPageObservedMs"],
                )
            else:
                record["emptyPages"] += 1
                record["lastEmptyResponseObservedMs"] = (observed - started) * 1000
                require(not completed_before_page, "first_result_missing")
        if useful:
            phase_deadline = deadline
            reason = "first_result_completion_timeout"
            remaining(phase_deadline, reason)
        if useful and completed_before_page:
            break
        status = host.command(
            "get_analysis_job",
            {"job_id": job},
            timeout=remaining(phase_deadline, reason),
        )
        verify_snapshot(status, job, size, source)
        remaining(phase_deadline, reason)
        if (
            status["state"] == "completed"
            and "completedJobStatusObservedMs" not in record
        ):
            record["completedJobStatusObservedMs"] = (clock() - started) * 1000
        if useful and status["state"] == "completed":
            break
        time.sleep(min(0.02, remaining(phase_deadline, reason)))
    require(
        status.get("complete") is True
        and not status.get("reason")
        and counter(status.get("scannedLines")) == rows
        and counter(status.get("resultRecords")) == 1,
        "first_result_incomplete",
    )
    require(
        status.get("captureBackend") == "Qt QRegularExpression", "first_result_backend"
    )
    record.update(
        complete=True,
        phase="completed",
        scannedLines=rows,
        resultRecords=1,
        captureBackend=status["captureBackend"],
    )
    LOG.info(
        "first_result_completed job_id=%s first_ms=%.3f completed_status_ms=%.3f rows=%d",
        job,
        record["firstUsefulPageObservedMs"],
        record["completedJobStatusObservedMs"],
        rows,
    )


def release_job(host, record):
    job = record.get("jobId")
    if not job:
        return
    prior_failure = sys.exc_info()[0] is not None
    host.deadline = clock() + 8  # Cleanup allowance is separate from measured phases.
    try:
        host.command("cancel_analysis_job", {"job_id": job}, timeout=host.remaining(2))
        host.release(job)
        expired = host.command(
            "get_analysis_job",
            {"job_id": job},
            timeout=host.remaining(2),
            allow_error=True,
        )
        require(expired.get("reason") == "job_expired", "first_result_not_released")
        record["released"] = True
        LOG.info("first_result_released job_id=%s expired=true", job)
    except (OSError, ValueError, RuntimeError) as error:
        record["cleanupFailure"] = type(error).__name__
        LOG.error(
            "first_result_cleanup_failed job_id=%s kind=%s", job, type(error).__name__
        )
        if not prior_failure:
            raise


def run_first_results(options, manifest, report):
    expected = fixture(manifest)
    require(expected["bytes"] <= 64 * 1024**2, "first_result_fixture_limit")
    report.update(
        timingBoundary="Before regex_evaluate submission through decoded/validated public response",
        completionBoundary="First completed get_analysis_job response; may precede first useful delivery",
        timingIncludes=[
            "protocol",
            "worker_startup",
            "compilation",
            "source_reading",
            "matching",
            "polling",
        ],
        timingExcludes=["application_startup", "open_index", "cleanup"],
        budgetKind="operational_guards_not_calibrated_release_budgets",
        sourcePolicy="immutable marker line followed by hash-validated ASCII corpus",
        resultPageSize=1,
    )
    report["unmeasured"] += [
        "exactServerFirstResultReadyTime",
        "firstResultCompositorPresentation",
    ]
    for repetition in range(options.repetitions):
        deadline = clock() + 180
        output = options.output / f"first-results-{repetition}"
        output.mkdir()
        source = output / "first-results.log"
        original = options.corpus / "ascii.log"
        require(
            file_hash(original) == expected["sha256"], "first_result_fixture_changed"
        )
        with source.open("wb") as target, original.open("rb") as input_stream:
            target.write((TOKEN + "\n").encode("ascii"))
            shutil.copyfileobj(input_stream, target, 1024**2)
        size, source_hash = source.stat().st_size, file_hash(source)
        require(
            size == expected["bytes"] + len(TOKEN) + 1
            and file_hash(original) == expected["sha256"],
            "first_result_fixture_changed",
        )
        payload, private = prepare_payload(options.executable, output)
        settings = output / "settings"
        settings.mkdir()
        environment = dict(
            os.environ,
            CILOGG_AUTOMATION="1",
            PYTHONUTF8="1",
            APPDATA=str(settings),
            LOCALAPPDATA=str(settings),
            XDG_CONFIG_HOME=str(settings),
            XDG_DATA_HOME=str(settings),
        )
        environment.pop("QT_QPA_PLATFORM", None)
        if options.platform == "offscreen":
            environment["QT_QPA_PLATFORM"] = "offscreen"
        run = dict(
            repetition=repetition,
            platform=options.platform,
            complete=False,
            executedBinary=private.relative_to(options.output).as_posix(),
            executedBinarySha256=file_hash(private),
            sourceSha256=source_hash,
            corpusFileSha256=expected["sha256"],
            rawBytes=size,
            physicalLines=expected["rows"] + 1,
            cycles=[],
        )
        report["runs"].append(run)
        with ExitStack() as stack:
            environment = stack.enter_context(private_ipc_environment(environment))
            with FirstResultsHost(
                private, output / "host", environment, False, deadline, options.platform
            ) as host:
                run["startupSeconds"] = clock() - host.started
                catalog = host.send(
                    "tools/call",
                    {"name": "cilogg_commands", "arguments": {"area": "commander"}},
                )["structuredContent"]
                require_operations(
                    catalog,
                    {
                        "regex_evaluate",
                        "get_analysis_job",
                        "get_analysis_results",
                        "cancel_analysis_job",
                        "release_analysis_job",
                        "open_file",
                    },
                )
                started = clock()
                index_deadline = min(
                    deadline, started + report["budgets"]["openIndexSeconds"]
                )
                host.command(
                    "open_file",
                    {"file": str(source)},
                    timeout=remaining(index_deadline, "first_result_index_timeout"),
                )
                wait_state(
                    host,
                    source,
                    index_deadline,
                    lambda state: not state["loadingInProgress"],
                )
                run["openIndexSecondsIncludingProtocol"] = clock() - started
                for cycle in range(2):
                    record = dict(
                        cycle=cycle,
                        hostAnalysisState="first_request"
                        if cycle == 0
                        else "repeated_request",
                        indexedSource=True,
                    )
                    run["cycles"].append(record)
                    host.deadline = deadline
                    try:
                        observe_first_results(
                            host,
                            size,
                            expected["rows"] + 1,
                            report["budgets"],
                            record,
                            deadline,
                        )
                    finally:
                        release_job(host, record)
                    require(
                        file_hash(source) == source_hash, "first_result_fixture_changed"
                    )
        run["complete"] = True
        run["ownedProcessesReaped"] = True
        LOG.info("first_result_host_complete repetition=%d cycles=2", repetition)
