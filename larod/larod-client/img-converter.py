import click
import PIL.Image
import numpy as np

@click.command()
@click.option("-i", "--input", "input_path", type=click.Path(exists=True), help="Input image path")
@click.option("-w", "--width", type=int, help="Width of the resized image")
@click.option("-h", "--height", type=int, help="Height of the resized image")
def convert_image(input_path, width, height):
    try:
        with PIL.Image.open(input_path) as img:
            img = img.convert("RGB")          # ensure 3-channel RGB
            resized_img = img.resize((width, height))

        image_array = np.array(resized_img, dtype=np.uint8)
        image_array.tofile("dog.bin")
    except PIL.UnidentifiedImageError as err:
        click.echo(f"Error: {err}", err=True)

if __name__ == "__main__":
    convert_image()
