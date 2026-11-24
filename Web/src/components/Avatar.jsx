// Round avatar with the name's first letter, colored by a hash of the
// name so everybody keeps the same color on every client.

import { initial, nameHue } from '../chat/names.js'

export default function Avatar({ name, size = 34 }) {
  return (
    <span className="avatar" aria-hidden
          style={{ background: `hsl(${nameHue(name)} 55% 45%)`, width: size, height: size,
                   fontSize: size * 0.45 }}>
      {initial(name)}
    </span>
  )
}
