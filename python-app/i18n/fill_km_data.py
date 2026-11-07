# Khmer draft translations, keyed by exact English <source>.
#
# Authored as a starting point for refinement in cutpro_km.ts. Terms are common
# video-editor vocabulary; genuinely ambiguous single words whose Khmer depends
# on context, brand/proper nouns, and pure unit/number strings are omitted on
# purpose (they fall back to English until refined).
#
# Consumed by fill_km.py.

DICT = {
    # --- core actions ---
    "OK": "យល់ព្រម", "Cancel": "បោះបង់", "Close": "បិទ", "Save": "រក្សាទុក",
    "Save...": "រក្សាទុក...", "Delete": "លុប", "Edit": "កែសម្រួល",
    "Copy": "ចម្លង", "Copied": "បានចម្លង", "Rename": "ប្តូរឈ្មោះ",
    "Create": "បង្កើត", "Search": "ស្វែងរក", "Undo": "មិនធ្វើវិញ",
    "Redo": "ធ្វើឡើងវិញ", "Reset": "កំណត់ឡើងវិញ", "Done": "រួចរាល់",
    "Exit": "ចាកចេញ", "Stop": "បញ្ឈប់", "Show": "បង្ហាញ", "Hide": "លាក់",
    "Add Text": "បន្ថែមអក្សរ", "Edit text": "កែសម្រួលអក្សរ",
    "Export": "នាំចេញ", "Import": "នាំចូល", "Download": "ទាញយក",
    "Install": "ដំឡើង", "Generate": "បង្កើត", "Browse": "រកមើល",
    "Change...": "ផ្លាស់ប្តូរ...", "Expand": "ពង្រីក", "Collapse": "បង្រួម",
    "Activate": "ធ្វើឲ្យសកម្ម", "Recheck": "ពិនិត្យឡើងវិញ",
    "Refresh": "ធ្វើឲ្យស្រស់", "Bypass": "រំលង", "Test key": "សាកល្បងកូនសោ",
    "Add API": "បន្ថែម API", "Go to end": "ទៅចុងបញ្ចប់",
    "Close Gap": "បិទចន្លោះ", "Setup": "ការដំឡើង",

    # --- navigation / sections ---
    "Home": "ទំព័រដើម", "Settings": "ការកំណត់", "General": "ទូទៅ",
    "Projects": "គម្រោង", "Project": "គម្រោង", "Media": "មេឌៀ",
    "Timeline": "បន្ទាត់ពេលវេលា", "Effects": "បែបផែន", "Effect": "បែបផែន",
    "Playback": "ការចាក់សំឡេង", "Language": "ភាសា", "License": "អាជ្ញាបណ្ណ",
    "Provider": "អ្នកផ្តល់សេវា", "Location": "ទីតាំង", "Startup": "ពេលចាប់ផ្តើម",
    "Sidebar": "របារចំហៀង", "Top Bar": "របារខាងលើ", "Captions": "អក្សររត់",
    "Graphics": "ក្រាហ្វិក", "Auto Save": "រក្សាទុកស្វ័យប្រវត្តិ",
    "Presets": "កំណត់ជាមុន", "Preset": "កំណត់ជាមុន", "Favorites": "សំណព្វ",
    "Devices": "ឧបករណ៍", "Tracks": "ផ្លូវ", "Meters": "ឧបករណ៍វាស់",

    # --- common nouns / properties ---
    "Name": "ឈ្មោះ", "Size": "ទំហំ", "Color": "ពណ៌", "Text": "អក្សរ",
    "Font": "ពុម្ពអក្សរ", "Audio": "សំឡេង", "Video": "វីដេអូ",
    "Voice": "សំឡេងនិយាយ", "Clip": "ក្លីប", "Mask": "របាំង", "Masks": "របាំង",
    "Format": "ទ្រង់ទ្រាយ", "Codec": "កូដិក", "Quality": "គុណភាព",
    "Duration": "រយៈពេល", "Position": "ទីតាំង", "Rotation": "ការបង្វិល",
    "Scale": "មាត្រដ្ឋាន", "Opacity": "ភាពថ្លា", "Volume": "កម្រិតសំឡេង",
    "Speed": "ល្បឿន", "Motion": "ចលនា", "Crop": "កាត់", "Blur": "ព្រិល",
    "Output": "លទ្ធផល", "Source": "ប្រភព", "Sequence": "លំដាប់",
    "Profile": "ប្រវ៉ូហ្វាល", "Channels": "ឆានែល", "Balance": "តុល្យភាព",
    "Panner": "ការចែកសំឡេង", "Pan": "ចែកសំឡេង", "Master volume": "កម្រិតសំឡេងមេ",
    "Mute all audio": "បិទសំឡេងទាំងអស់", "Latency": "ការយឺត",
    "Marker": "សញ្ញាសម្គាល់", "Model": "ម៉ូដែល", "Engine": "ម៉ាស៊ីន",
    "Address": "អាសយដ្ឋាន", "Port": "ច្រក", "Base URL": "URL គោល",
    "API key": "កូនសោ API", "API keys": "កូនសោ API", "Key": "កូនសោ",
    "File": "ឯកសារ", "File name": "ឈ្មោះឯកសារ", "Duration": "រយៈពេល",
    "Estimated": "ប៉ាន់ស្មាន", "Info": "ព័ត៌មាន", "Level": "កម្រិត",
    "Amount": "បរិមាណ", "Strength": "កម្លាំង", "Padding": "គម្លាតខាងក្នុង",
    "Feather": "ធ្វើឲ្យរលោង", "Tracking": "ការតាមដាន", "Look": "រូបរាង",
    "Tone": "សំនៀង", "Tint": "ពណ៌លាំៗ", "Hue": "ពណ៌", "Hex": "Hex",

    # --- states / qualifiers ---
    "Active": "សកម្ម", "Expired": "ផុតកំណត់", "Status": "ស្ថានភាព",
    "Ready": "រួចរាល់", "Required": "ត្រូវការ", "Enabled": "បានបើក",
    "Disabled": "បានបិទ", "optional": "ស្រេចចិត្ត", "Running": "កំពុងដំណើរការ",
    "Working": "កំពុងធ្វើការ", "Idle": "ទំនេរ", "Complete": "បានបញ្ចប់",
    "Failed": "បរាជ័យ", "Cancelled": "បានបោះបង់", "Selected": "បានជ្រើស",
    "Imported": "បាននាំចូល", "Inverted": "បានបញ្ច្រាស", "Custom": "ផ្ទាល់ខ្លួន",
    "None": "គ្មាន", "All": "ទាំងអស់", "High": "ខ្ពស់", "Low": "ទាប",
    "Fast": "លឿន", "Balanced": "មធ្យម", "Natural": "ធម្មជាតិ",
    "Classic": "បុរាណ", "Creative": "ច្នៃប្រឌិត", "Bold": "ដិត",
    "Italic": "ទ្រេត", "Main": "មេ", "Fit": "សម", "Boxed": "ក្នុងប្រអប់",

    # --- directions / alignment ---
    "Left": "ឆ្វេង", "Right": "ស្តាំ", "Top": "កំពូល", "Bottom": "បាត",
    "Align": "តម្រឹម", "Midpoint": "ចំណុចកណ្តាល",

    # --- units of time (words only) ---
    "minutes": "នាទី", "seconds": "វិនាទី",

    # --- languages ---
    "English": "អង់គ្លេស", "Khmer": "ខ្មែរ", "French": "បារាំង",
    "German": "អាល្លឺម៉ង់", "Spanish": "អេស្ប៉ាញ", "Chinese": "ចិន",
    "Japanese": "ជប៉ុន", "Korean": "កូរ៉េ",

    # --- color grading (Lumetri) ---
    "Contrast": "កម្រិតពណ៌ផ្ទុយ", "Exposure": "ការបំភ្លឺ",
    "Brightness": "ពន្លឺ", "Interface": "ចំណុចប្រទាក់",
    "Highlight color": "ពណ៌បន្លិច", "Highlight": "បន្លិច",
    "Shadows": "ស្រមោល", "Whites": "ពណ៌ស", "Blacks": "ពណ៌ខ្មៅ",
    "Vibrance": "ភាពរស់រវើក", "Vignette": "ស្រមោលគែម", "Curves": "ខ្សែកោង",
    "Sharpen": "ធ្វើឲ្យច្បាស់", "Denoise": "កាត់បន្ថយសំឡេងរំខាន",
    "Vocal separation": "ការបំបែកសំឡេងច្រៀង", "Animation": "ចលនា",
    "Balance": "តុល្យភាព", "Mono": "ម៉ូណូ", "Stereo": "ស្តេរ៉េអូ",
}
