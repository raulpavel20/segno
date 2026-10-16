namespace fd::ui
{
namespace
{
constexpr float kStroke = 1.5f;

/// Icons are defined on a 16 x 16 grid; `filled` paths are filled, the rest stroked.
struct Glyph
{
    juce::Path path;
    bool filled = false;
    juce::Path solid; // extra filled shapes on a stroked icon
};

Glyph glyph(Icon icon)
{
    Glyph g;
    auto& p = g.path;
    switch (icon)
    {
    case Icon::Play:
        p.addTriangle(4.5f, 3.0f, 4.5f, 13.0f, 13.0f, 8.0f);
        g.filled = true;
        break;
    case Icon::Stop:
        p.addRectangle(4.0f, 4.0f, 8.0f, 8.0f);
        g.filled = true;
        break;
    case Icon::Record:
        p.addEllipse(3.5f, 3.5f, 9.0f, 9.0f);
        g.filled = true;
        break;
    case Icon::Loop:
        // A rounded loop: the top runs right, the bottom runs left, each with an arrow head.
        p.startNewSubPath(2.5f, 9.0f);
        p.lineTo(2.5f, 7.0f);
        p.quadraticTo(2.5f, 4.5f, 5.0f, 4.5f);
        p.lineTo(12.5f, 4.5f);
        p.startNewSubPath(10.5f, 2.5f);
        p.lineTo(12.5f, 4.5f);
        p.lineTo(10.5f, 6.5f);
        p.startNewSubPath(13.5f, 7.0f);
        p.lineTo(13.5f, 9.0f);
        p.quadraticTo(13.5f, 11.5f, 11.0f, 11.5f);
        p.lineTo(3.5f, 11.5f);
        p.startNewSubPath(5.5f, 9.5f);
        p.lineTo(3.5f, 11.5f);
        p.lineTo(5.5f, 13.5f);
        break;
    case Icon::Metronome:
        p.startNewSubPath(5.5f, 13.5f);
        p.lineTo(7.0f, 2.5f);
        p.lineTo(9.0f, 2.5f);
        p.lineTo(10.5f, 13.5f);
        p.closeSubPath();
        p.startNewSubPath(8.0f, 11.0f);
        p.lineTo(12.5f, 5.0f);
        break;
    case Icon::PanelLeft:
    case Icon::PanelRight:
    case Icon::PanelBottom:
        p.addRoundedRectangle(2.0f, 3.0f, 12.0f, 10.0f, 1.5f);
        if (icon == Icon::PanelLeft)
        {
            p.startNewSubPath(6.0f, 3.0f);
            p.lineTo(6.0f, 13.0f);
        }
        else if (icon == Icon::PanelRight)
        {
            p.startNewSubPath(10.0f, 3.0f);
            p.lineTo(10.0f, 13.0f);
        }
        else
        {
            p.startNewSubPath(2.0f, 9.5f);
            p.lineTo(14.0f, 9.5f);
        }
        break;
    case Icon::ChevronLeft:
        p.startNewSubPath(10.0f, 3.5f);
        p.lineTo(5.5f, 8.0f);
        p.lineTo(10.0f, 12.5f);
        break;
    case Icon::ChevronRight:
        p.startNewSubPath(6.0f, 3.5f);
        p.lineTo(10.5f, 8.0f);
        p.lineTo(6.0f, 12.5f);
        break;
    case Icon::Plus:
        p.startNewSubPath(8.0f, 3.0f);
        p.lineTo(8.0f, 13.0f);
        p.startNewSubPath(3.0f, 8.0f);
        p.lineTo(13.0f, 8.0f);
        break;
    case Icon::Close:
        p.startNewSubPath(4.5f, 4.5f);
        p.lineTo(11.5f, 11.5f);
        p.startNewSubPath(11.5f, 4.5f);
        p.lineTo(4.5f, 11.5f);
        break;
    case Icon::Capture:
        // An arrow turning back round a dot: what was just played, kept.
        p.addCentredArc(8.0f, 8.0f, 5.5f, 5.5f, 0.0f, -2.2f, 2.6f, true);
        p.startNewSubPath(1.6f, 3.4f);
        p.lineTo(3.3f, 4.9f);
        p.lineTo(5.4f, 3.9f);
        g.solid.addEllipse(5.5f, 5.5f, 5.0f, 5.0f);
        break;
    case Icon::Follow:
        // The playhead, and the view going on with it.
        p.startNewSubPath(4.5f, 2.5f);
        p.lineTo(4.5f, 13.5f);
        p.startNewSubPath(7.5f, 8.0f);
        p.lineTo(13.5f, 8.0f);
        p.startNewSubPath(11.0f, 5.5f);
        p.lineTo(13.5f, 8.0f);
        p.lineTo(11.0f, 10.5f);
        break;
    case Icon::CountIn:
        // Three beats into the downbeat.
        for (float x : {2.75f, 6.0f, 9.25f})
            g.solid.addEllipse(x - 1.25f, 9.75f, 2.5f, 2.5f);
        p.startNewSubPath(13.25f, 3.0f);
        p.lineTo(13.25f, 13.0f);
        break;
    case Icon::ChevronDown:
        p.startNewSubPath(3.5f, 6.0f);
        p.lineTo(8.0f, 10.5f);
        p.lineTo(12.5f, 6.0f);
        break;
    case Icon::Audition:
        // A speaker, and the sound coming from it.
        g.solid.startNewSubPath(2.0f, 6.0f);
        g.solid.lineTo(4.5f, 6.0f);
        g.solid.lineTo(8.5f, 2.5f);
        g.solid.lineTo(8.5f, 13.5f);
        g.solid.lineTo(4.5f, 10.0f);
        g.solid.lineTo(2.0f, 10.0f);
        g.solid.closeSubPath();
        p.addCentredArc(9.0f, 8.0f, 2.5f, 2.5f, 0.0f, 0.75f, 2.39f, true);
        p.addCentredArc(9.0f, 8.0f, 5.0f, 5.0f, 0.0f, 0.75f, 2.39f, true);
        break;
    case Icon::Settings:
        // Three sliders.
        for (int i = 0; i < 3; ++i)
        {
            const float y = 4.0f + 4.0f * static_cast<float>(i);
            p.startNewSubPath(2.5f, y);
            p.lineTo(13.5f, y);
            const float knob = i == 1 ? 10.0f : 5.5f;
            p.addEllipse(knob - 1.5f, y - 1.5f, 3.0f, 3.0f);
        }
        break;
    case Icon::Keyboard:
        // A piano: four white keys (outline and gaps below) and three short, wide black keys,
        // so it reads as a keyboard rather than a grid even at 16 px.
        p.addRoundedRectangle(1.0f, 3.5f, 14.0f, 9.0f, 1.0f);
        for (float x : {4.5f, 8.0f, 11.5f})
        {
            p.startNewSubPath(x, 8.5f);
            p.lineTo(x, 12.5f);
            g.solid.addRoundedRectangle(x - 1.5f, 3.5f, 3.0f, 4.5f, 0.5f);
        }
        break;
    case Icon::AutomationArm:
        // An automation line through its points.
        p.startNewSubPath(1.5f, 12.0f);
        p.lineTo(5.5f, 6.0f);
        p.lineTo(10.0f, 10.0f);
        p.lineTo(14.5f, 3.5f);
        for (auto [x, y] : {std::pair{5.5f, 6.0f}, std::pair{10.0f, 10.0f}})
            g.solid.addEllipse(x - 1.75f, y - 1.75f, 3.5f, 3.5f);
        break;
    case Icon::AutomationOn:
        // The line, coming back round: a turning arrow under a short lane.
        p.startNewSubPath(1.5f, 7.0f);
        p.lineTo(5.0f, 3.5f);
        p.lineTo(8.5f, 6.0f);
        p.lineTo(14.5f, 2.0f);
        p.addCentredArc(8.0f, 11.0f, 3.5f, 3.5f, 0.0f, 0.6f, 5.2f, true);
        p.startNewSubPath(10.2f, 9.6f);
        p.lineTo(11.3f, 12.2f);
        p.lineTo(8.6f, 12.6f);
        break;
    case Icon::Assistant:
        // A four-pointed spark, with a small one beside it.
        p.startNewSubPath(7.0f, 2.0f);
        p.quadraticTo(7.6f, 8.4f, 13.0f, 9.0f);
        p.quadraticTo(7.6f, 9.6f, 7.0f, 15.0f);
        p.quadraticTo(6.4f, 9.6f, 1.0f, 9.0f);
        p.quadraticTo(6.4f, 8.4f, 7.0f, 2.0f);
        p.closeSubPath();
        p.startNewSubPath(12.5f, 1.0f);
        p.quadraticTo(12.8f, 3.2f, 15.0f, 3.5f);
        p.quadraticTo(12.8f, 3.8f, 12.5f, 6.0f);
        p.quadraticTo(12.2f, 3.8f, 10.0f, 3.5f);
        p.quadraticTo(12.2f, 3.2f, 12.5f, 1.0f);
        p.closeSubPath();
        g.filled = true;
        break;
    case Icon::Minimise:
        p.startNewSubPath(4.5f, 8.0f);
        p.lineTo(11.5f, 8.0f);
        break;
    case Icon::Maximise:
        p.addRectangle(4.5f, 4.5f, 7.0f, 7.0f);
        break;
    case Icon::Restore:
        p.addRectangle(4.5f, 6.5f, 5.0f, 5.0f);
        p.startNewSubPath(6.5f, 6.5f);
        p.lineTo(6.5f, 4.5f);
        p.lineTo(11.5f, 4.5f);
        p.lineTo(11.5f, 9.5f);
        p.lineTo(9.5f, 9.5f);
        break;
    case Icon::Star:
    case Icon::StarFilled:
        p.addStar({8.0f, 8.3f}, 5, 2.6f, 6.0f);
        g.filled = icon == Icon::StarFilled;
        break;
    case Icon::Note:
        // An eighth note: a filled head, its stem and flag.
        g.solid.addEllipse(2.5f, 10.0f, 5.0f, 3.8f);
        p.startNewSubPath(7.2f, 11.8f);
        p.lineTo(7.2f, 2.5f);
        p.quadraticTo(9.0f, 5.5f, 12.5f, 6.5f);
        break;
    case Icon::Knob:
        // A knob: its ring and pointer, and the ticks of its travel.
        p.addEllipse(3.0f, 3.0f, 10.0f, 10.0f);
        p.startNewSubPath(8.0f, 8.0f);
        p.lineTo(5.2f, 5.2f);
        p.startNewSubPath(1.8f, 13.2f);
        p.lineTo(2.8f, 12.2f);
        p.startNewSubPath(14.2f, 13.2f);
        p.lineTo(13.2f, 12.2f);
        break;
    case Icon::Plug:
        // A plug: two prongs, the body, and the cable.
        p.startNewSubPath(6.0f, 1.5f);
        p.lineTo(6.0f, 4.5f);
        p.startNewSubPath(10.0f, 1.5f);
        p.lineTo(10.0f, 4.5f);
        p.addRoundedRectangle(3.5f, 4.5f, 9.0f, 5.5f, 1.5f);
        p.startNewSubPath(8.0f, 10.0f);
        p.lineTo(8.0f, 14.5f);
        break;
    case Icon::Folder:
        // A folder: its body and tab.
        p.startNewSubPath(1.5f, 4.0f);
        p.lineTo(6.0f, 4.0f);
        p.lineTo(7.5f, 5.5f);
        p.lineTo(14.5f, 5.5f);
        p.lineTo(14.5f, 13.0f);
        p.lineTo(1.5f, 13.0f);
        p.closeSubPath();
        break;
    case Icon::Rack:
        // Two modules stacked, each with its knob.
        p.addRoundedRectangle(2.0f, 2.5f, 12.0f, 4.75f, 1.2f);
        p.addRoundedRectangle(2.0f, 8.75f, 12.0f, 4.75f, 1.2f);
        g.solid.addEllipse(4.0f, 3.9f, 2.0f, 2.0f);
        g.solid.addEllipse(4.0f, 10.15f, 2.0f, 2.0f);
        break;
    case Icon::Lock:
        // A padlock: its shackle over its body.
        p.startNewSubPath(5.0f, 7.0f);
        p.lineTo(5.0f, 5.0f);
        p.cubicTo(5.0f, 1.0f, 11.0f, 1.0f, 11.0f, 5.0f);
        p.lineTo(11.0f, 7.0f);
        g.solid.addRoundedRectangle(3.0f, 7.0f, 10.0f, 7.5f, 1.5f);
        break;
    case Icon::LockOpen:
        // The shackle lifted clear of the body on its right.
        p.startNewSubPath(5.0f, 7.0f);
        p.lineTo(5.0f, 3.5f);
        p.cubicTo(5.0f, -0.5f, 11.0f, -0.5f, 11.0f, 3.5f);
        p.lineTo(11.0f, 4.5f);
        g.solid.addRoundedRectangle(3.0f, 7.0f, 10.0f, 7.5f, 1.5f);
        break;
    case Icon::Modulator:
        // One cycle of a wave, and a dot where it is now (at its crest).
        p.startNewSubPath(1.5f, 8.0f);
        p.cubicTo(3.0f, 1.5f, 6.5f, 1.5f, 8.0f, 8.0f);
        p.cubicTo(9.5f, 14.5f, 13.0f, 14.5f, 14.5f, 8.0f);
        g.solid.addEllipse(3.3f, 1.9f, 3.0f, 3.0f);
        break;
    case Icon::Move:
        // A cross with an arrowhead at each end.
        p.startNewSubPath(8.0f, 2.0f);
        p.lineTo(8.0f, 14.0f);
        p.startNewSubPath(2.0f, 8.0f);
        p.lineTo(14.0f, 8.0f);
        p.startNewSubPath(5.5f, 4.5f);
        p.lineTo(8.0f, 2.0f);
        p.lineTo(10.5f, 4.5f);
        p.startNewSubPath(5.5f, 11.5f);
        p.lineTo(8.0f, 14.0f);
        p.lineTo(10.5f, 11.5f);
        p.startNewSubPath(4.5f, 5.5f);
        p.lineTo(2.0f, 8.0f);
        p.lineTo(4.5f, 10.5f);
        p.startNewSubPath(11.5f, 5.5f);
        p.lineTo(14.0f, 8.0f);
        p.lineTo(11.5f, 10.5f);
        break;
    case Icon::Transform:
        // A low bar turned into a high one.
        p.startNewSubPath(2.0f, 11.5f);
        p.lineTo(7.0f, 11.5f);
        p.startNewSubPath(9.0f, 4.5f);
        p.lineTo(14.0f, 4.5f);
        p.startNewSubPath(4.5f, 9.0f);
        p.cubicTo(5.5f, 6.0f, 7.0f, 4.5f, 9.0f, 4.5f);
        break;
    case Icon::Generate:
        // A plus over lines.
        p.startNewSubPath(8.0f, 2.0f);
        p.lineTo(8.0f, 7.5f);
        p.startNewSubPath(5.25f, 4.75f);
        p.lineTo(10.75f, 4.75f);
        p.startNewSubPath(3.0f, 10.5f);
        p.lineTo(13.0f, 10.5f);
        p.startNewSubPath(3.0f, 13.5f);
        p.lineTo(9.0f, 13.5f);
        break;
    case Icon::Quantise:
        // A note between two grid lines.
        p.startNewSubPath(3.5f, 2.5f);
        p.lineTo(3.5f, 13.5f);
        p.startNewSubPath(12.5f, 2.5f);
        p.lineTo(12.5f, 13.5f);
        p.addRectangle(3.5f, 6.5f, 5.5f, 3.0f);
        break;
    case Icon::Humanize:
        p.startNewSubPath(2.0f, 9.0f);
        p.cubicTo(4.0f, 5.0f, 5.5f, 5.0f, 7.0f, 8.0f);
        p.cubicTo(8.5f, 11.0f, 10.0f, 11.0f, 12.0f, 7.5f);
        p.lineTo(14.0f, 6.0f);
        break;
    case Icon::Strum:
        for (const auto [x0, y] : {std::pair{2.0f, 12.5f}, std::pair{5.0f, 8.5f}, std::pair{8.0f, 4.5f}})
        {
            p.startNewSubPath(x0, y);
            p.lineTo(x0 + 5.5f, y);
        }
        break;
    case Icon::Arpeggiate:
        for (const auto [x0, y] :
             {std::pair{2.0f, 13.0f}, std::pair{5.5f, 10.0f}, std::pair{9.0f, 7.0f}, std::pair{12.5f, 4.0f}})
        {
            p.startNewSubPath(x0, y);
            p.lineTo(x0 + 2.0f, y);
        }
        break;
    case Icon::Chop:
        for (float x0 : {2.0f, 6.5f, 11.0f})
            g.solid.addRoundedRectangle(x0, 6.5f, 3.0f, 3.0f, 0.75f);
        break;
    case Icon::Ornament:
        p.startNewSubPath(2.5f, 6.0f);
        p.lineTo(4.5f, 6.0f);
        p.startNewSubPath(6.5f, 10.0f);
        p.lineTo(14.0f, 10.0f);
        break;
    case Icon::Velocity:
        for (const auto [x0, top] : {std::pair{4.0f, 9.0f}, std::pair{8.0f, 3.5f}, std::pair{12.0f, 6.5f}})
        {
            p.startNewSubPath(x0, 14.0f);
            p.lineTo(x0, top);
            p.startNewSubPath(x0 - 1.5f, top);
            p.lineTo(x0 + 1.5f, top);
        }
        break;
    case Icon::Span:
        p.startNewSubPath(2.0f, 8.0f);
        p.lineTo(10.5f, 8.0f);
        p.startNewSubPath(8.0f, 5.5f);
        p.lineTo(10.5f, 8.0f);
        p.lineTo(8.0f, 10.5f);
        p.startNewSubPath(13.5f, 3.0f);
        p.lineTo(13.5f, 13.0f);
        break;
    case Icon::Randomise:
        p.addRoundedRectangle(2.5f, 2.5f, 11.0f, 11.0f, 2.0f);
        for (const auto [cx, cy] : {std::pair{5.75f, 5.75f}, std::pair{10.25f, 10.25f},
                                    std::pair{10.25f, 5.75f}, std::pair{5.75f, 10.25f}})
            g.solid.addEllipse(cx - 1.0f, cy - 1.0f, 2.0f, 2.0f);
        break;
    case Icon::MakeChords:
        p.startNewSubPath(2.0f, 12.5f);
        p.lineTo(5.5f, 12.5f);
        for (float y : {4.5f, 8.5f, 12.5f})
        {
            p.startNewSubPath(8.0f, y);
            p.lineTo(14.0f, y);
        }
        break;
    case Icon::Rhythm:
        for (float cx : {2.75f, 6.25f, 9.75f, 13.25f})
            g.solid.addEllipse(cx - 1.25f, 5.75f, 2.5f, 2.5f);
        p.startNewSubPath(2.0f, 12.0f);
        p.lineTo(14.0f, 12.0f);
        break;
    case Icon::Melody:
        p.startNewSubPath(2.0f, 11.0f);
        p.lineTo(5.0f, 7.0f);
        p.lineTo(8.0f, 9.5f);
        p.lineTo(11.0f, 4.5f);
        p.lineTo(14.0f, 7.0f);
        break;
    case Icon::Chords:
        for (float y : {4.5f, 8.0f, 11.5f})
        {
            p.startNewSubPath(3.0f, y);
            p.lineTo(13.0f, y);
        }
        break;
    case Icon::Bassline:
        for (const auto [x0, y] : {std::pair{2.0f, 12.0f}, std::pair{6.5f, 13.5f}, std::pair{10.5f, 11.0f}})
        {
            p.startNewSubPath(x0, y);
            p.lineTo(x0 + 3.5f, y);
        }
        p.startNewSubPath(5.0f, 4.5f);
        p.lineTo(11.0f, 4.5f);
        break;
    case Icon::Normalise:
        // Two levels raised to a line.
        p.startNewSubPath(2.0f, 2.5f);
        p.lineTo(14.0f, 2.5f);
        for (const float x0 : {5.0f, 11.0f})
        {
            p.startNewSubPath(x0, 14.0f);
            p.lineTo(x0, 6.0f);
            p.startNewSubPath(x0 - 2.0f, 8.0f);
            p.lineTo(x0, 6.0f);
            p.lineTo(x0 + 2.0f, 8.0f);
        }
        break;
    case Icon::Tempo:
        // Beats found in a sound: hits along a line, a tick over each.
        p.startNewSubPath(1.5f, 13.5f);
        p.lineTo(14.5f, 13.5f);
        for (const float x0 : {3.5f, 8.0f, 12.5f})
        {
            p.startNewSubPath(x0, 13.5f);
            p.lineTo(x0, 8.5f);
            p.startNewSubPath(x0, 2.5f);
            p.lineTo(x0, 5.0f);
        }
        break;
    case Icon::Slice:
        // A sound cut at its hits: each hit's attack and decay a piece, gaps between them.
        for (const float x0 : {1.5f, 6.5f, 11.5f})
        {
            p.startNewSubPath(x0, 13.5f);
            p.lineTo(x0, 3.0f);
            p.lineTo(x0 + 3.0f, 13.5f);
            p.closeSubPath();
        }
        break;
    case Icon::Search:
        p.addEllipse(2.5f, 2.5f, 8.5f, 8.5f);
        p.startNewSubPath(9.8f, 9.8f);
        p.lineTo(13.5f, 13.5f);
        break;
    case Icon::Undo:
    case Icon::Redo:
    {
        p.startNewSubPath(4.0f, 6.0f);
        p.lineTo(10.0f, 6.0f);
        p.cubicTo(15.0f, 6.0f, 15.0f, 13.0f, 10.0f, 13.0f);
        p.lineTo(6.0f, 13.0f);
        p.startNewSubPath(6.5f, 3.5f);
        p.lineTo(4.0f, 6.0f);
        p.lineTo(6.5f, 8.5f);
        if (icon == Icon::Redo)
            p.applyTransform(juce::AffineTransform::scale(-1.0f, 1.0f, 8.0f, 8.0f));
        break;
    }
    }
    return g;
}
} // namespace

void drawIcon(juce::Graphics& g, Icon icon, juce::Rectangle<float> area, juce::Colour colour)
{
    const float size = std::min(area.getWidth(), area.getHeight());
    const float scale = size / 16.0f;
    const auto box = juce::Rectangle<float>(size, size).withCentre(area.getCentre());
    auto glyphData = glyph(icon);
    glyphData.path.applyTransform(juce::AffineTransform::scale(scale).translated(box.getX(), box.getY()));
    g.setColour(colour);
    if (!glyphData.solid.isEmpty())
    {
        glyphData.solid.applyTransform(juce::AffineTransform::scale(scale).translated(box.getX(), box.getY()));
        g.fillPath(glyphData.solid);
    }
    if (glyphData.filled)
        g.fillPath(glyphData.path);
    else
        g.strokePath(glyphData.path, juce::PathStrokeType(kStroke * std::max(1.0f, scale * 0.9f),
                                                          juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::ScaledImage cursorImage(Icon icon)
{
    // The icon at a system cursor's size, ringed by a halo as much thicker as the icon is larger
    // than its 16 px grid, with room for it; drawn at twice the size.
    const auto& t = Theme::current();
    const auto size = static_cast<float>(t.input.cursorIconSize);
    const float halo = size / 16.0f;
    const int room = static_cast<int>(std::ceil(halo)) + 1;
    const int side = t.input.cursorIconSize + 2 * room;
    constexpr int scale = 2;
    juce::Image image(juce::Image::ARGB, side * scale, side * scale, true);
    {
        juce::Graphics g(image);
        g.addTransform(juce::AffineTransform::scale(static_cast<float>(scale)));
        const auto area = juce::Rectangle<float>(static_cast<float>(room), static_cast<float>(room), size, size);
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                if (dx != 0 || dy != 0)
                    drawIcon(g, icon, area.translated(static_cast<float>(dx) * halo, static_cast<float>(dy) * halo), t.surface0);
        drawIcon(g, icon, area, t.textPrimary);
    }
    return {image, static_cast<double>(scale)};
}

juce::MouseCursor cursorFor(Icon icon)
{
    const auto image = cursorImage(icon);
    const int centre = juce::roundToInt(image.getImage().getWidth() / image.getScale() / 2.0);
    return juce::MouseCursor(image, {centre, centre});
}

IconButton::IconButton(Icon icon, const juce::String& name) : juce::Button(name), icon_(icon) {}

void IconButton::setIcon(Icon icon)
{
    icon_ = icon;
    repaint();
}

void IconButton::setIconSize(int size)
{
    iconSize_ = size;
    repaint();
}

void IconButton::setRecordAccent(bool record)
{
    record_ = record;
    repaint();
}

void IconButton::mouseDown(const juce::MouseEvent& e)
{
    popup_ = e.mods.isPopupMenu() && onPopupMenu != nullptr;
    if (popup_)
    {
        onPopupMenu();
        return;
    }
    juce::Button::mouseDown(e);
}

void IconButton::mouseUp(const juce::MouseEvent& e)
{
    if (std::exchange(popup_, false))
        return;
    juce::Button::mouseUp(e);
}

void IconButton::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const auto& t = Theme::current();
    const auto r = getLocalBounds().toFloat();
    const bool on = getToggleState();
    if (on)
    {
        const auto fill = record_ ? t.record : t.active;
        g.setColour(down ? fill.darker(0.15f) : fill);
        g.fillRoundedRectangle(r, t.radiusControl);
    }
    else if (highlighted || down)
    {
        g.setColour(down ? t.surface3.brighter(0.08f) : t.surface3);
        g.fillRoundedRectangle(r, t.radiusControl);
    }
    const auto colour = on ? t.onActive : (isEnabled() ? (highlighted ? t.textPrimary : t.textSecondary) : t.textTertiary);
    drawIcon(g, icon_,
             iconSize_ > 0
                 ? r.withSizeKeepingCentre(static_cast<float>(iconSize_), static_cast<float>(iconSize_))
                 : r.reduced(r.getHeight() * 0.2f),
             colour);
}
} // namespace fd::ui
