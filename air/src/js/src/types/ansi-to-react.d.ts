declare module 'ansi-to-react' {
  import * as React from 'react'
  export interface AnsiProps {
    children?: React.ReactNode
  }
  const Ansi: React.FC<AnsiProps>
  export default Ansi
}


