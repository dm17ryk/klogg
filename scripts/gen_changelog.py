import os
import subprocess
import re
from datetime import date, datetime

from dataclasses import dataclass
from typing import Dict, List, Tuple
from collections import defaultdict

git_log_proc = subprocess.run(['git', '--no-pager', 'log', '--format=%as %h: %s (%an)', 'v20.12..'], capture_output=True)
log = git_log_proc.stdout.decode("utf-8").splitlines()

log_regex = re.compile("([0-9\-]{10}) ([a-z0-9]+){8}: ([a-z]+):(.*)")
simple_log_regex = re.compile("([0-9\-]{10}) ([a-z0-9]{8}):(.*)")

@dataclass
class CommitMessage:
    date: date
    sha: str
    group: str
    message: str

YearMonthKey = Tuple[int,int]
CommitsList = List[CommitMessage]
CommitsGroup = Dict[str, CommitsList]
commits: Dict[YearMonthKey, CommitsGroup] = defaultdict(lambda: defaultdict(lambda: []))

for log_line in log:
    if not log_line:
        continue

    simple_match = simple_log_regex.match(log_line)
    if not simple_match:
        continue

    commit_date = datetime.strptime(simple_match.group(1), '%Y-%m-%d').date()
    commit_sha = simple_match.group(2).strip()
    commit_message = simple_match.group(3).strip()
    commit_group = 'other'

    match = log_regex.match(log_line)
    if match and len(match.groups()) == 4:
        commit_group = match.group(3).strip()
        commit_message = match.group(4).strip()

    if commit_group == 'feat':
        commit_group = 'feature'

    if commit_group == 'doc':
        commit_group = 'docs'

    if 'feature' in commit_message:
        commit_group = 'feature'
    if 'ump version' in commit_message:
        commit_group = 'chore'
    if 'ump latest version' in commit_message:
        commit_group = 'chore'
    if 'ix build' in commit_message:
        commit_group = 'build'
    if 'ix test' in commit_message:
        commit_group = 'tests'

    key: YearMonthKey = (commit_date.year, commit_date.month)
    commits_this_month = commits[key]
    commits_this_month[commit_group].append(CommitMessage(commit_date, commit_sha, commit_group, commit_message))

groups_mapping = [ 
    ('feature', 'New features'), 
    ('fixes', 'Bug fixes'), 
    ('docs', 'Documentation'),
    ('perf', 'Performance'),
    ('refactor', 'Code refactoring'), 
    ('tests', 'Tests'), 
    ('build', 'Build system'), 
    ('ci', 'Continuous integration workflow'),
    ('other', 'Other commits')
]

months_keys = sorted(commits.keys())
months_keys.reverse()

for key in months_keys:
    print(f'# {key[0]}-{key[1]:02}:')
    for g, header in groups_mapping:
        commits_in_g = commits[key][g]
        if not commits_in_g:
            continue
        print(f'## {header}:')
        for c in commits_in_g:
            print(f' - [{c.sha}](https://github.com/dm17ryk/klogg/commit/{c.sha}): {c.message}')