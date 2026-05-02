import {html, LitElement} from "lit"
import {customElement, state} from "lit/decorators.js"
import '../components/filedrag.js'
import FEATURES from "../features.js"
import {cuteAlert} from "../utils/feedback.js"
import {elrsState} from "../utils/state.js"

@customElement('update-panel')
class UpdatePanel extends LitElement {
    @state() accessor progress = 0
    @state() accessor progressText = ''

    createRenderRoot() {
        this._completeHandler = this._completeHandler.bind(this)
        this._progressHandler = this._progressHandler.bind(this)
        return this
    }

    render() {
        const version = elrsState.settings?.version || ''
        const gitHash = elrsState.settings?.['git-commit'] || ''
        const eyebrowParts = []
        if (version) eyebrowParts.push('v' + version)
        if (gitHash) eyebrowParts.push(gitHash)
        return html`
            <div class="td-page-head" style="margin-bottom: var(--td-s-4);">
                <div>
                    ${eyebrowParts.length ? html`<div class="td-eyebrow">Currently installed<span class="td-eyebrow-sep"> &middot; </span>${eyebrowParts.join(' · ')}</div>` : ''}
                    <div class="td-h2">Firmware Update</div>
                </div>
            </div>
            <div class="td-card">
                <div class="td-card-header">
                    <span class="td-h4">Flash firmware</span>
                </div>
                <div class="td-card-body">
                    <p class="td-body td-mute" style="margin-bottom: var(--td-s-3);">
                        Select the correct <strong class="td-fg">firmware.bin${FEATURES.IS_8285 ? '.gz' : ''}</strong> for your platform.
                        You can also <a href="firmware.bin" style="color: var(--td-brand);">download the running firmware</a>.
                    </p>
                    <file-drop id="firmware-upload" label="Select firmware file" @file-drop="${this._fileSelectHandler}">
                        or drop firmware file here
                    </file-drop>
                    ${this.progressText ? html`
                        <div class="td-progress-wrap">
                            <div class="td-progress-label">${this.progressText}</div>
                            <progress class="td-progress" value="${this.progress}" max="100"></progress>
                        </div>
                    ` : ''}
                </div>
            </div>
        `
    }

    _fileSelectHandler(e) {
        const files = e.detail.files
        const fileExt = files[0].name.split('.').pop()
        let expectedFileExt, expectedFileExtDesc
        if (FEATURES.IS_8285 && !FEATURES.IS_TX) {
            expectedFileExt = 'gz'
            expectedFileExtDesc = '.bin.gz file. Do NOT decompress the file.'
        } else {
            expectedFileExt = 'bin'
            expectedFileExtDesc = '.bin file.'
        }
        if (fileExt === expectedFileExt) {
            this._uploadFile(files[0])
        } else {
            cuteAlert({
                type: 'error',
                title: 'Incorrect File Format',
                message: 'Selected "' + files[0].name + '" — must be a ' + expectedFileExtDesc
            })
        }
    }

    _uploadFile(file) {
        const formdata = new FormData()
        formdata.append('upload', file, file.name)
        const ajax = new XMLHttpRequest()
        ajax.upload.addEventListener('progress', this._progressHandler, false)
        ajax.addEventListener('load', this._completeHandler, false)
        ajax.addEventListener('error', this._errorHandler, false)
        ajax.addEventListener('abort', this._abortHandler, false)
        ajax.open('POST', '/update')
        ajax.setRequestHeader('X-FileSize', file.size)
        ajax.send(formdata)
    }

    _progressHandler(event) {
        const percent = Math.round((event.loaded / event.total) * 100)
        this.progress = percent
        this.progressText = percent + '% uploaded'
        this.requestUpdate()
    }

    _completeHandler(event) {
        const self = this
        this.progressText = ''
        this.progress = 0
        const data = JSON.parse(event.target.responseText)
        if (data.status === 'ok') {
            function showMessage() {
                cuteAlert({ type: 'success', title: 'Update Succeeded', message: data.msg })
            }
            let percent = 0
            const interval = setInterval(() => {
                percent = percent + (FEATURES.IS_8285 ? 1 : 2)
                self.progress = percent
                self.progressText = percent + '% flashed'
                if (percent === 100) {
                    clearInterval(interval)
                    self.progressText = ''
                    self.progress = 0
                    showMessage()
                }
                self.requestUpdate()
            }, 100)
        } else if (data.status === 'mismatch') {
            cuteAlert({
                type: 'question',
                title: 'Targets Mismatch',
                message: data.msg,
                confirmText: 'Flash anyway',
                cancelText: 'Cancel'
            }).then((e) => {
                const xmlhttp = new XMLHttpRequest()
                xmlhttp.onreadystatechange = function() {
                    if (this.readyState === 4) {
                        self.progressText = ''
                        self.progress = 0
                        self.requestUpdate()
                        cuteAlert({
                            type: this.status === 200 ? 'info' : 'error',
                            title: 'Force Update',
                            message: this.status === 200 ? JSON.parse(this.responseText).msg : 'An error occurred'
                        })
                    }
                }
                xmlhttp.open('POST', '/forceupdate', true)
                const data = new FormData()
                data.append('action', e)
                xmlhttp.send(data)
            })
        } else {
            cuteAlert({ type: 'error', title: 'Update Failed', message: data.msg })
        }
        this.requestUpdate()
    }

    _errorHandler(event) {
        this.progressText = ''
        this.progress = 0
        return cuteAlert({ type: 'error', title: 'Update Failed', message: event.target.responseText })
    }

    _abortHandler(event) {
        this.progressText = ''
        this.progress = 0
        return cuteAlert({ type: 'info', title: 'Update Aborted', message: event.target.responseText })
    }
}
