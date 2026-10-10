import {unsafeHTML} from 'lit/directives/unsafe-html.js'

// Render a TitanLRS design-system icon (assets/td.js) inside a lit template.
// Kept in its own module: rollup-plugin-minify-template-literals skips HTML minification for any
// file that calls unsafeHTML() directly, so pages import this instead.
export const tdIcon = name => unsafeHTML(TD.icon(name))
