import AnsiToHtml from 'ansi-to-html'

const converter = new AnsiToHtml({
  fg: '#000',
  bg: '#fff',
  newline: true,
  escapeXML: true,
  stream: false,
})

type Props = { text: string }

export default function AnsiText({ text }: Props) {
  const __html = converter.toHtml(text)
  return (
    <span dangerouslySetInnerHTML={{ __html }} />
  )
}


