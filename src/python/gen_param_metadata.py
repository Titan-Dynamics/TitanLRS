#!/usr/bin/env python3
"""Build the GCS metadata for the TitanLRS MAVLink parameters from the doc comments on the
parameter tables (src/tx_mavlink.cpp, src/rx-serial/rx_mavlink.cpp).

  --check            validate only (every table entry documented, every doc comment has an entry,
                     tags well formed); exit status 1 on any problem
  --xml PATH         write TitanLRS.apm.pdef.xml (ArduPilot's apm.pdef.xml format, loaded by
                     Titan Planner for components whose HEARTBEAT carries the TLRS marker)
  --markdown PATH    write the parameter reference

Doc comment format, directly above each table entry:

    // @Param: TX_LINK_MODE
    // @DisplayName: Link mode
    // @Description: ...
    // @Values: 0:Normal,1:MAVLink
    // @User: Standard
    {"TX_LINK_MODE", hasRadio, getLinkMode, setLinkMode, nullptr},

Tags: Param, DisplayName, Description, User (Standard/Advanced) are required; Values, Range
("min max"), Units, Increment, ReadOnly (True), Hardware are optional. Hardware is appended to the
description, because the file is the same for every board. ReadOnly must match the entry having no
setter.
"""
import argparse
import os
import re
import sys
from xml.sax.saxutils import escape, quoteattr

SRC = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
COMPONENTS = [
    ('TX', 'TX module (component 241)', os.path.join(SRC, 'src', 'tx_mavlink.cpp')),
    ('RX', 'Receiver (component 68)', os.path.join(SRC, 'src', 'rx-serial', 'rx_mavlink.cpp')),
]
VEHICLE = 'TitanLRS'
REQUIRED = ('Param', 'DisplayName', 'Description', 'User')
OPTIONAL = ('Values', 'Range', 'Units', 'Increment', 'ReadOnly', 'Hardware')

TABLE_START = re.compile(r'static const MavParam \w+\[\]\s*=\s*\{')
DOC = re.compile(r'^\s*//\s*@(\w+):\s*(.*?)\s*$')
ENTRY = re.compile(r'^\s*\{\s*"([^"]+)"\s*,(.*)\}\s*,?\s*$')
NAME = re.compile(r'^[A-Z0-9_]{1,16}$')


class Problems(list):
    def add(self, path, line, text):
        self.append('%s:%d: %s' % (os.path.relpath(path, SRC), line, text))


def parse_values(text):
    values = []
    for item in text.split(','):
        code, sep, label = item.partition(':')
        code = code.strip()
        label = label.strip()
        if not sep or not re.match(r'^-?\d+$', code) or not label:
            raise ValueError('bad value "%s" (want code:label)' % item)
        if ':' in label:
            raise ValueError('label "%s" contains ":" (Mission Planner splits on it)' % label)
        values.append((int(code), label))
    return values


def parse_file(path, problems):
    params = []
    with open(path) as f:
        lines = f.read().split('\n')
    start = next((i for i, l in enumerate(lines) if TABLE_START.search(l)), None)
    if start is None:
        problems.add(path, 1, 'no "static const MavParam ...[] = {" table found')
        return params
    doc, doc_line = {}, None
    for n in range(start + 1, len(lines)):
        line = lines[n]
        if line.strip().startswith('};'):
            break
        m = DOC.match(line)
        if m:
            tag, value = m.groups()
            if tag not in REQUIRED + OPTIONAL:
                problems.add(path, n + 1, 'unknown tag @%s' % tag)
            elif tag == 'Param' and doc:
                problems.add(path, doc_line, '@Param %s has no table entry' % doc.get('Param'))
                doc = {}
            if tag in doc:
                problems.add(path, n + 1, 'duplicate @%s' % tag)
            doc[tag] = value
            doc_line = doc_line if doc_line and tag != 'Param' else n + 1
            continue
        m = ENTRY.match(line)
        if not m:
            continue
        name, rest = m.groups()
        fields = [f.strip() for f in rest.split(',')]
        if not doc:
            problems.add(path, n + 1, 'table entry %s has no doc comment' % name)
            continue
        if doc.get('Param') != name:
            problems.add(path, n + 1, 'doc comment @Param %s is above table entry %s' % (doc.get('Param'), name))
        for tag in REQUIRED:
            if tag not in doc:
                problems.add(path, doc_line, '%s: missing @%s' % (name, tag))
        if not NAME.match(name):
            problems.add(path, n + 1, '%s: names are 1-16 of A-Z 0-9 _' % name)
        if doc.get('User') not in (None, 'Standard', 'Advanced'):
            problems.add(path, doc_line, '%s: @User must be Standard or Advanced' % name)
        read_only = len(fields) >= 3 and fields[2] == 'nullptr'
        if read_only != (doc.get('ReadOnly') == 'True'):
            problems.add(path, n + 1, '%s: @ReadOnly %s but the entry %s a setter' %
                         (name, doc.get('ReadOnly', 'absent'), 'has no' if read_only else 'has'))
        values = []
        if 'Values' in doc:
            try:
                values = parse_values(doc['Values'])
            except ValueError as e:
                problems.add(path, doc_line, '%s: @Values %s' % (name, e))
        if 'Range' in doc and not re.match(r'^-?[\d.]+ -?[\d.]+$', doc['Range']):
            problems.add(path, doc_line, '%s: @Range must be "min max"' % name)
        params.append(dict(doc, Param=name, values=values, line=n + 1))
        doc, doc_line = {}, None
    if doc:
        problems.add(path, doc_line, '@Param %s has no table entry' % doc.get('Param'))
    return params


def documentation(p):
    text = p['Description']
    if 'Hardware' in p:
        text += ' (%s.)' % p['Hardware'].rstrip('.')
    return text


def write_xml(components, path):
    out = ['<?xml version="1.0" encoding="utf-8"?>',
           '<!-- Generated by python/gen_param_metadata.py from the TitanLRS MAVLink parameter tables -->',
           '<paramfile>', '  <vehicles>', '    <parameters name=%s>' % quoteattr(VEHICLE)]
    for _, _, params in components:
        for p in params:
            out.append('      <param humanName=%s name=%s documentation=%s user=%s>' % (
                quoteattr(p['DisplayName']), quoteattr('%s:%s' % (VEHICLE, p['Param'])),
                quoteattr(documentation(p)), quoteattr(p['User'])))
            if p['values']:
                out.append('        <values>')
                out += ['          <value code="%d">%s</value>' % (c, escape(l)) for c, l in p['values']]
                out.append('        </values>')
            for field in ('Units', 'Range', 'Increment', 'ReadOnly'):
                if field in p:
                    out.append('        <field name="%s">%s</field>' % (field, escape(p[field])))
            out.append('      </param>')
    out += ['    </parameters>', '  </vehicles>', '</paramfile>', '']
    with open(path, 'w') as f:
        f.write('\n'.join(out))


def write_markdown(components, path):
    out = ['# TitanLRS MAVLink parameters', '',
           'Generated by `python/gen_param_metadata.py` from the parameter tables; do not edit.', '',
           'With the TX in MAVLink link mode, the TX and the receiver each appear to the GCS as a component '
           'of the vehicle (same system ID as the flight controller), with their own parameter list. '
           'Values are whole numbers sent as floats. A write that is refused is answered with the unchanged '
           'value and a STATUSTEXT giving the reason. Parameters a board does not have are not listed.', '',
           '- **Component IDs:** TX 241 (`MAV_COMP_ID_UART_BRIDGE`), receiver 68 (`MAV_COMP_ID_TELEMETRY_RADIO`). '
           'Build-time overrides: `-DTLRS_MAV_TX_COMPID=` / `-DTLRS_MAV_RX_COMPID=`.',
           '- **System ID:** taken from the first autopilot HEARTBEAT seen and kept for the session. Neither '
           'component sends a HEARTBEAT until then, or for 5 s after start (then the TX uses 1 and the receiver '
           'its Target SysID).',
           '- **Discovery:** each component sends a HEARTBEAT (TX: 1 Hz, on the GCS side only; receiver: when its '
           'link comes up, then every 10 s, to save airtime) with `autopilot = MAV_AUTOPILOT_INVALID`, '
           '`type = MAV_TYPE_GENERIC` and `custom_mode = 0x544C5253` (\"TLRS\"), which Titan Planner uses to load '
           '`TitanLRS.apm.pdef.xml`. MAVFTP requests are refused at once, so a GCS uses the parameter protocol.',
           '- **The flight controller never sees these components:** their frames only travel towards the GCS.',
           '- **Receiver writes** are saved by the receiver, which drops the link for a moment; the reply is sent first.',
           '']
    for key, title, params in components:
        out += ['## %s' % title, '', '| Parameter | Name | Description | Values |', '|---|---|---|---|']
        for p in params:
            values = ', '.join('%d: %s' % v for v in p['values'])
            if 'Range' in p:
                values = (values + '; ' if values else '') + 'range %s' % p['Range'].replace(' ', ' to ')
            if 'Units' in p:
                values = (values + '; ' if values else '') + p['Units']
            name = p['DisplayName'] + (' (read-only)' if p.get('ReadOnly') == 'True' else '')
            out.append('| `%s` | %s | %s | %s |' % (p['Param'], name, documentation(p).replace('|', '\\|'),
                                                  values.replace('|', '\\|')))
        out.append('')
    with open(path, 'w') as f:
        f.write('\n'.join(out))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--check', action='store_true', help='validate only')
    ap.add_argument('--xml', help='write the apm.pdef.xml metadata file here')
    ap.add_argument('--markdown', help='write the Markdown parameter reference here')
    args = ap.parse_args()

    problems = Problems()
    components = [(key, title, parse_file(path, problems)) for key, title, path in COMPONENTS]
    seen = {}
    for key, _, params in components:
        for p in params:
            if p['Param'] in seen:
                problems.append('%s defined in both %s and %s' % (p['Param'], seen[p['Param']], key))
            seen[p['Param']] = key
    if problems:
        print('\n'.join(problems), file=sys.stderr)
        sys.exit(1)
    if args.xml:
        write_xml(components, args.xml)
    if args.markdown:
        write_markdown(components, args.markdown)
    print('%d parameters documented (%s)' % (len(seen), ', '.join(
        '%s %d' % (key, len(params)) for key, _, params in components)))


if __name__ == '__main__':
    main()
