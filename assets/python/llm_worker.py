"""截图大模型调用模块，使用环境变量配置 OpenAI 兼容的视觉模型服务。"""

import base64
import os

from openai import OpenAI


def analyze_screenshot(image_png: bytes, task: str) -> str:
    """把截图发送给视觉模型并返回文字提取结果或内容总结。

    参数：image_png 为 PNG 图片的原始字节；task 为 extract_text 或 summarize。
    返回：模型生成的中文文本。
    异常：缺少密钥、任务类型错误或接口请求失败时抛出异常，由 C++ 层显示。
    """
    api_key = os.environ.get("OPENAI_API_KEY", "").strip()
    if not api_key:
        raise RuntimeError("请先设置 OPENAI_API_KEY 环境变量。")
    if task not in {"extract_text", "summarize"}:
        raise ValueError("不支持的截图分析类型。")
    if not image_png:
        raise ValueError("截图内容为空。")

    model = os.environ.get("OPENAI_MODEL", "gpt-4o-mini").strip()
    base_url = os.environ.get("OPENAI_BASE_URL", "").strip() or None
    client = OpenAI(api_key=api_key, base_url=base_url, timeout=60.0, max_retries=1)
    encoded_image = base64.b64encode(image_png).decode("ascii")
    if task == "extract_text":
        instruction = (
            "请识别截图中清晰可见的全部文字，尽量保留原有段落、列表和换行。"
            "只输出识别结果；不确定的内容标注为 [无法辨认]，不要补写截图之外的信息。"
        )
    else:
        instruction = (
            "请用中文概括这张截图的主要内容。优先说明截图展示的对象、关键信息和可见状态，"
            "简洁分点回答；不确定的内容明确说明，不要推测截图之外的信息。"
        )

    response = client.chat.completions.create(
        model=model,
        messages=[
            {
                "role": "user",
                "content": [
                    {"type": "text", "text": instruction},
                    {
                        "type": "image_url",
                        "image_url": {"url": f"data:image/png;base64,{encoded_image}"},
                    },
                ],
            }
        ],
        max_tokens=1800,
    )
    content = response.choices[0].message.content
    if not content or not content.strip():
        raise RuntimeError("大模型返回了空结果。")
    return content.strip()
